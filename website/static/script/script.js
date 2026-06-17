var qrcode = new QRCode("qrcode");

async function makeCode() {
  const elLogin = document.getElementById("login"); // utilisé comme identifiant de connexion

  try {
    const response = await fetch(`/api/etudiant?login=${encodeURIComponent(elLogin.value)}`);

    if (!response.ok) {
      const err = await response.json();
      alert("Erreur : " + (err.error || "inconnue"));
      return;
    }

    const data = await response.json();

    // data = { prenom, nom, id }
    const qr = "nom='" + data.nom + "';prenom='" + data.prenom + "';idEtudiant='" + data.id + "'";
    qrcode.makeCode(qr);

  } catch (e) {
    alert("Erreur réseau : " + e.message);
  }
}

function downloadQRCode() {

    const img = document.getElementById("qrcode").querySelector("img");
    if (!img) return alert("QR non généré !");
  
    const a = document.createElement("a");
    a.href = img.src; 
    a.download = "qrcode.png";
    a.click();
}

(() => {
  'use strict';

  const form = document.querySelector('.needs-validation');
  const downloadOrNot = document.getElementById("downlad");

  form.addEventListener('submit', event => {
    event.preventDefault();
    event.stopPropagation();

    const isValid = form.checkValidity();
    form.classList.add('was-validated');

    if (isValid) {
      makeCode();
    }
  });

  downloadOrNot.addEventListener("click", event => {
    event.preventDefault();

    const isValid = form.checkValidity();
    form.classList.add("was-validated");

    if (isValid) {
      downloadQRCode();
    }
  });

})();


(() => {
    'use strict'

    const getStoredTheme = () => localStorage.getItem('theme')
    const setStoredTheme = theme => localStorage.setItem('theme', theme)

    const getPreferredTheme = () => {
        const storedTheme = getStoredTheme()
        if (storedTheme) return storedTheme
        return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'
    }

    const setTheme = theme => {
        if (theme === 'auto') {
            document.documentElement.setAttribute(
                'data-bs-theme',
                window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'
            )
        } else {
            document.documentElement.setAttribute('data-bs-theme', theme)
        }
    }

    const showActiveTheme = (theme, focus = false) => {
        const themeSwitcher = document.querySelector('#bd-theme')
        if (!themeSwitcher) return

        // Mettre à jour l'icône du bouton principal
        const icon = themeSwitcher.querySelector('.theme-icon-active')
        const btnToActive = document.querySelector(`[data-bs-theme-value="${theme}"] i`)
        if (btnToActive) icon.className = btnToActive.className + ' theme-icon-active'

        // Mettre à jour l'état actif dans le menu
        document.querySelectorAll('[data-bs-theme-value]').forEach(el => {
            el.classList.remove('active')
            el.setAttribute('aria-pressed', 'false')
        })
        const btnActive = document.querySelector(`[data-bs-theme-value="${theme}"]`)
        btnActive.classList.add('active')
        btnActive.setAttribute('aria-pressed', 'true')

        if (focus) themeSwitcher.focus()
    }

    setTheme(getPreferredTheme())

    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
        const storedTheme = getStoredTheme()
        if (storedTheme !== 'light' && storedTheme !== 'dark') {
            setTheme(getPreferredTheme())
            showActiveTheme(getPreferredTheme())
        }
    })

    window.addEventListener('DOMContentLoaded', () => {
        showActiveTheme(getPreferredTheme())
        document.querySelectorAll('[data-bs-theme-value]').forEach(toggle => {
            toggle.addEventListener('click', () => {
                const theme = toggle.getAttribute('data-bs-theme-value')
                setStoredTheme(theme)
                setTheme(theme)
                showActiveTheme(theme, true)
            })
        })
    })
})();

