#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <math.h>
#include <time.h>
#include <wiringPi.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#define SERIAL_PORT      "/dev/serial0"
#define BAUD_RATE        B9600
#define BUF_SIZE         8192
#define BUZZER_PIN       1
#define BIP_MS           150
#define ANIM_DURATION_MS 3000
#define SW               800
#define SH               480

/* Adapter ce chemin selon où tu mets les fichiers sur le Pi */
#define PYTHON_SCRIPT    "check_presence.py"
#define PYTHON_BIN       "python3"

typedef enum { STATE_WAITING, STATE_LOADING, STATE_SUCCESS, STATE_FAILURE } AppState;

typedef struct {
    AppState state;
    char     last_nom[64];
    char     last_prenom[64];
    int      anim_start_ms;
    int      dirty;
    pthread_mutex_t lock;
} SharedData;
SharedData shared = { .state = STATE_WAITING, .dirty = 0 };

int get_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void bip() { digitalWrite(BUZZER_PIN, HIGH); delay(BIP_MS); digitalWrite(BUZZER_PIN, LOW); }

/*
 * Appelle check_presence.py avec l'id étudiant.
 * Retourne 1 si "true:NOM:PRENOM", remplit nom_out/prenom_out.
 * Retourne 0 si "false".
 */
int call_python(const char *id_etu, char *nom_out, char *prenom_out) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "%s %s \"%s\" 2>/dev/null",
             PYTHON_BIN, PYTHON_SCRIPT, id_etu);

    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;

    char result[256] = {0};
    if (fgets(result, sizeof(result), fp) == NULL) {
        pclose(fp);
        return 0;
    }
    pclose(fp);

    result[strcspn(result, "\r\n")] = '\0';

    /* Format succès : "true:NOM:PRENOM" */
    if (strncmp(result, "true:", 5) == 0) {
        char *sep = strchr(result + 5, ':');
        if (sep) {
            int nl = sep - (result + 5);
            if (nl > 0 && nl < 64) {
                strncpy(nom_out, result + 5, nl);
                nom_out[nl] = '\0';
            }
            strncpy(prenom_out, sep + 1, 63);
            prenom_out[63] = '\0';
        }
        return 1;
    }
    return 0;
}

void handle_scan(const char *id_etu) {
    printf("\n[SCAN] ID étudiant : %s — appel Python...\n", id_etu);
    fflush(stdout);

    /* Bip immédiat + écran orange dès la réception du scan */
    bip();
    pthread_mutex_lock(&shared.lock);
    shared.state = STATE_LOADING;
    shared.anim_start_ms = get_ms();
    shared.dirty = 1;
    pthread_mutex_unlock(&shared.lock);

    char nom[64]    = {0};
    char prenom[64] = {0};
    int ok = call_python(id_etu, nom, prenom);

    pthread_mutex_lock(&shared.lock);
    strncpy(shared.last_nom,    nom,    63);
    strncpy(shared.last_prenom, prenom, 63);
    shared.anim_start_ms = get_ms();

    if (ok) {
        shared.state = STATE_SUCCESS;
        printf("[RÉSULTAT] VERT — %s %s marqué présent\n", prenom, nom);
    } else {
        shared.state = STATE_FAILURE;
        printf("[RÉSULTAT] ROUGE — ID %s refusé\n", id_etu);
    }
    fflush(stdout);
    shared.dirty = 1;
    pthread_mutex_unlock(&shared.lock);
}

/*
 * Parse le buffer série.
 * Cherche "idEtudiant='X'" ou "id='X'" dans les données reçues.
 */
void process_buffer(char *accum, int *len) {
    accum[*len] = '\0';
    char *p = accum, *last = accum;

    while (p < accum + *len) {
        char *id_field = strstr(p, "idEtudiant='");
        char *id_short = strstr(p, "id='");

        char *found = NULL;
        if (id_field && (!id_short || id_field <= id_short)) {
            found = id_field + 12; /* après "idEtudiant='" */
        } else if (id_short) {
            found = id_short + 4;  /* après "id='" */
        }

        if (!found) break;

        char *end = strchr(found, '\'');
        if (!end) break;

        int il = end - found;
        if (il > 0 && il < 32) {
            char id_etu[32] = {0};
            strncpy(id_etu, found, il);
            handle_scan(id_etu);
        }

        last = end + 1;
        p    = last;
    }

    int rem = accum + *len - last;
    if (rem > 0 && rem < BUF_SIZE) memmove(accum, last, rem); else rem = 0;
    *len = rem;
}

void *serial_thread(void *arg) {
    int fd = open(SERIAL_PORT, O_RDONLY | O_NOCTTY);
    if (fd < 0) { perror("serial"); return NULL; }

    struct termios tty; memset(&tty, 0, sizeof tty);
    tcgetattr(fd, &tty); cfsetispeed(&tty, BAUD_RATE);
    tty.c_cflag = CS8 | CLOCAL | CREAD;
    tty.c_iflag = tty.c_lflag = tty.c_oflag = 0;
    tty.c_cc[VMIN] = 1; tty.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &tty);

    char accum[BUF_SIZE + 1]; int alen = 0;
    unsigned char buf[256];

    while (1) {
        int n = read(fd, buf, sizeof(buf));
        if (n <= 0) continue;
        for (int i = 0; i < n; i++)
            if (buf[i] >= 0x20 && buf[i] < 0x7F && alen < BUF_SIZE)
                accum[alen++] = buf[i];
        process_buffer(accum, &alen);
    }
    return NULL;
}

/* ── Rendu ────────────────────────────────────────────────────────────────── */
void fill_circle(SDL_Renderer *r, int cx, int cy, int rad) {
    for (int dy = -rad; dy <= rad; dy++) {
        int dx = (int)sqrt((double)(rad*rad - dy*dy));
        SDL_RenderDrawLine(r, cx-dx, cy+dy, cx+dx, cy+dy);
    }
}

void draw_text(SDL_Renderer *r, TTF_Font *f, const char *txt,
               Uint8 R, Uint8 G, Uint8 B, Uint8 A, int x, int y, int center) {
    SDL_Color c = {R, G, B, A};
    SDL_Surface *s = TTF_RenderUTF8_Blended(f, txt, c); if (!s) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_SetTextureAlphaMod(t, A);
    SDL_Rect dst = {center ? x - s->w/2 : x, y, s->w, s->h};
    SDL_RenderCopy(r, t, NULL, &dst);
    SDL_DestroyTexture(t); SDL_FreeSurface(s);
}

void draw_check(SDL_Renderer *r, int cx, int cy, int sz, float p) {
    float x0=cx-sz*.42f, y0=cy+sz*.05f, x1=cx-sz*.08f, y1=cy+sz*.38f,
          x2=cx+sz*.42f, y2=cy-sz*.32f;
    float s1=sqrtf((x1-x0)*(x1-x0)+(y1-y0)*(y1-y0));
    float s2=sqrtf((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
    float d = p*(s1+s2);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    for (int th = -4; th <= 4; th++) {
        if (d <= s1) { float f = d/s1;
            SDL_RenderDrawLine(r,(int)(x0+th),(int)y0,(int)(x0+(x1-x0)*f+th),(int)(y0+(y1-y0)*f));
        } else {
            SDL_RenderDrawLine(r,(int)(x0+th),(int)y0,(int)(x1+th),(int)y1);
            float f2=(d-s1)/s2;
            SDL_RenderDrawLine(r,(int)(x1+th),(int)y1,(int)(x1+(x2-x1)*f2+th),(int)(y1+(y2-y1)*f2));
        }
    }
}

void draw_cross(SDL_Renderer *r, int cx, int cy, int sz, float p) {
    float ax=cx-sz*.38f, ay=cy-sz*.38f, bx=cx+sz*.38f, by=cy+sz*.38f;
    float cx2=cx+sz*.38f, cy2=cy-sz*.38f, dx=cx-sz*.38f, dy2=cy+sz*.38f;
    float p1=fminf(p*2.f, 1.f), p2=fmaxf(p*2.f-1.f, 0.f);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    for (int th = -4; th <= 4; th++) {
        SDL_RenderDrawLine(r,(int)ax+th,(int)ay,(int)(ax+(bx-ax)*p1)+th,(int)(ay+(by-ay)*p1));
        if (p2 > 0) SDL_RenderDrawLine(r,(int)cx2+th,(int)cy2,(int)(cx2+(dx-cx2)*p2)+th,(int)(cy2+(dy2-cy2)*p2));
    }
}

void render(SDL_Renderer *r, TTF_Font *big, TTF_Font *med, TTF_Font *sml,
            AppState state, const char *nom, const char *prenom,
            int anim_start, int now) {

    float t = fminf((float)(now - anim_start) / (float)ANIM_DURATION_MS, 1.f);
    int cx = SW/2, cy = SH/2;

    if      (state == STATE_WAITING) SDL_SetRenderDrawColor(r, 12,  12,  32, 255);
    else if (state == STATE_LOADING) SDL_SetRenderDrawColor(r, 50,  30,   0, 255);
    else if (state == STATE_SUCCESS) SDL_SetRenderDrawColor(r,  5,  40,  20, 255);
    else                             SDL_SetRenderDrawColor(r, 45,   8,   8, 255);
    SDL_RenderClear(r);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    if (state == STATE_LOADING) {
        /* Cercle orange pulsant */
        float pulse = 0.7f + 0.3f * sinf((float)now / 400.f);
        int rad = (int)(70 * pulse);
        SDL_SetRenderDrawColor(r, 230, 130, 0, (Uint8)(180*pulse+75));
        fill_circle(r, cx, cy-60, rad);
        /* Trois points animés */
        int ndots = ((now / 500) % 4);
        char dots[5] = "    ";
        for (int i = 0; i < ndots; i++) dots[i] = '.';
        char loading[32]; snprintf(loading, sizeof(loading), "Connexion%s", dots);
        draw_text(r, big, loading,                    255,160, 30, 255, cx, cy+10, 1);
        draw_text(r, med, "Verification en cours...", 200,120, 20, 180, cx, cy+65, 1);

    } else if (state == STATE_WAITING) {
        float pulse = 0.6f + 0.4f * sinf((float)now / 700.f);
        int rad = (int)(70 * pulse);
        SDL_SetRenderDrawColor(r, 60, 110, 240, (Uint8)(180*pulse+75));
        fill_circle(r, cx, cy-60, rad);
        SDL_SetRenderDrawColor(r, 200, 220, 255, 255);
        SDL_Rect qr[] = {{cx-18,cy-76,12,12},{cx+6,cy-76,12,12},
                         {cx-18,cy-56,12,12},{cx+6,cy-56, 8, 8}};
        for (int i = 0; i < 4; i++) SDL_RenderFillRect(r, &qr[i]);
        draw_text(r, big, "En attente de scan",                        220,220,255,255, cx, cy+10, 1);
        draw_text(r, med, "Présentez votre QR code devant le lecteur", 140,150,200,200, cx, cy+65, 1);

    } else if (state == STATE_SUCCESS) {
        float ct = fminf(t/0.4f, 1.f); ct = 1.f-(1.f-ct)*(1.f-ct);
        int rad = (int)(90*ct);
        SDL_SetRenderDrawColor(r, 30, 180, 90, 255);
        fill_circle(r, cx, cy-50, rad);
        float cht = t < .4f ? 0.f : fminf((t-.4f)/.4f, 1.f);
        draw_check(r, cx, cy-50, 78, cht);
        if (t > .6f) {
            Uint8 a = (Uint8)(255*fminf((t-.6f)/.2f, 1.f));
            draw_text(r, big, "Accès accordé !",       80,255,150, a, cx, cy+60,  1);
            char full[140]; snprintf(full, sizeof(full), "%s %s", prenom, nom);
            draw_text(r, med, full,                   255,255,255, a, cx, cy+110, 1);
            draw_text(r, sml, "Présence enregistrée", 140,210,160, a, cx, cy+148, 1);
        }
        int bw = (int)((SW-80)*(1.f-fminf((float)(now-anim_start)/(float)(ANIM_DURATION_MS+500),1.f)));
        SDL_SetRenderDrawColor(r, 255,255,255,  40);
        SDL_Rect bg  = {40, SH-20, SW-80, 5}; SDL_RenderFillRect(r, &bg);
        SDL_SetRenderDrawColor(r, 255,255,255, 160);
        SDL_Rect bar = {40, SH-20, bw,    5}; SDL_RenderFillRect(r, &bar);

    } else { /* STATE_FAILURE */
        float ct = fminf(t/0.4f, 1.f); ct = 1.f-(1.f-ct)*(1.f-ct);
        int rad = (int)(90*ct);
        SDL_SetRenderDrawColor(r, 200, 40, 40, 255);
        fill_circle(r, cx, cy-50, rad);
        float crt = t < .4f ? 0.f : fminf((t-.4f)/.4f, 1.f);
        draw_cross(r, cx, cy-50, 78, crt);
        if (t > .6f) {
            Uint8 a = (Uint8)(255*fminf((t-.6f)/.2f, 1.f));
            draw_text(r, big, "Accès refusé",                           255,100,100, a, cx, cy+60,  1);
            draw_text(r, sml, "Déjà présent, inconnu ou pas de cours.", 255,200, 80, a, cx, cy+148, 1);
        }
        int bw = (int)((SW-80)*(1.f-fminf((float)(now-anim_start)/(float)(ANIM_DURATION_MS+500),1.f)));
        SDL_SetRenderDrawColor(r, 255,255,255,  40);
        SDL_Rect bg  = {40, SH-20, SW-80, 5}; SDL_RenderFillRect(r, &bg);
        SDL_SetRenderDrawColor(r, 255,255,255, 160);
        SDL_Rect bar = {40, SH-20, bw,    5}; SDL_RenderFillRect(r, &bar);
    }

    SDL_RenderPresent(r);
}

int main(void) {
    wiringPiSetup();
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    pthread_mutex_init(&shared.lock, NULL);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    if (TTF_Init() < 0)               { fprintf(stderr, "TTF: %s\n", TTF_GetError()); return 1; }

    SDL_ShowCursor(SDL_DISABLE);
    SDL_Window *win = SDL_CreateWindow("QR", 0, 0, SW, SH,
        SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS);
    SDL_RaiseWindow(win);
    SDL_SetWindowInputFocus(win);

    SDL_Renderer *rend = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);

    const char *fonts[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf", NULL};
    TTF_Font *big = NULL, *med = NULL, *sml = NULL;
    for (int i = 0; fonts[i] && !big; i++) {
        big = TTF_OpenFont(fonts[i], 36);
        med = TTF_OpenFont(fonts[i], 24);
        sml = TTF_OpenFont(fonts[i], 18);
    }
    if (!big) { fprintf(stderr, "Police introuvable\n"); return 1; }

    pthread_t tid;
    pthread_create(&tid, NULL, serial_thread, NULL);
    printf("Démarré. En attente de scans...\n"); fflush(stdout);

    AppState cur_state = STATE_WAITING;
    char cur_nom[64] = {0}, cur_prenom[64] = {0};
    int cur_anim_start = 0, last_render = 0;

    while (1) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            if (ev.type == SDL_QUIT ||
               (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE))
                goto quit;

        pthread_mutex_lock(&shared.lock);
        if (shared.dirty) {
            cur_state = shared.state;
            strncpy(cur_nom,    shared.last_nom,    63);
            strncpy(cur_prenom, shared.last_prenom, 63);
            cur_anim_start = shared.anim_start_ms;
            shared.dirty = 0;
        }
        pthread_mutex_unlock(&shared.lock);

        int now = get_ms();
        if (cur_state != STATE_WAITING && cur_state != STATE_LOADING &&
            (now - cur_anim_start) > ANIM_DURATION_MS + 800)
            cur_state = STATE_WAITING;

        if (now - last_render >= 40) {
            render(rend, big, med, sml, cur_state, cur_nom, cur_prenom, cur_anim_start, now);
            last_render = now;
        } else {
            SDL_Delay(5);
        }
    }

quit:
    TTF_CloseFont(big); TTF_CloseFont(med); TTF_CloseFont(sml);
    SDL_DestroyRenderer(rend); SDL_DestroyWindow(win);
    TTF_Quit(); SDL_Quit();
    return 0;
}
