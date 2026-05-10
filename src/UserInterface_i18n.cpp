#include "UserInterface.h"
#include "UserInterface_i18n.h"
#include <cstring>

const char* languages[] = { "EN", "FR", "DE", "SP", "IT" };
const int NUM_LANGUAGES = sizeof(languages) / sizeof(languages[0]);

static const char* translations[][KEY_COUNT] = {
	// 18 characters max (size 1)

    // EN
    { "USB Keyboard  ", "USB Mouse     ", "USB Joystick  ", "USB Mouse enabled", "Joy 0 enabled", "Mouse speed", "Language", "USB Keyboard",
	  "Help", "USB<->Atari mouse", "Reset", "Joy1 D-sub<->USB", "Joy0 D-sub<->USB",
	  "Set CPU 270 MHz", "Set CPU 150 MHz", "Joy dead-zone", "Back", "Settings", "Debug"},
    // FR
    { "Clavier USB   ", "Souris USB    ", "Joystick USB  ", "Souris USB activée", "Joy 0 activé", "Vitesse souris", "Langue", "Clavier USB",
	  "Aide", "Souris USB<->Atari", "Réinitialiser", "Joy1 D-sub<->USB", "Joy0 D-sub<->USB",
	  "Activer 270 MHz", "Activer 150 MHz", "Zone morte joy.", "Retour", "Paramètres", "Debogage"},
    // DE
    { "USB-Tastatur  ", "USB-Maus      ", "USB-Joystick  ", "USB-Maus aktiviert", "Joy 0 aktivert", "Maus-geschw.", "Sprache", "USB-Tastatur",
	  "Hilfe", "Maus USB<->Atari", "Zurücksetzen", "Joy1 D-sub<->USB", "Joy0 D-sub<->USB",
	  "CPU 270 MHz", "CPU 150 MHz", "Joystick-Totzone", "Zurück", "Einstellungen", "Debuggen"},
    // SP
    { "Teclado USB   ", "Ratón USB     ", "Joystick USB  ", "Ratón USB activado", "Joy 0 activado", "Velocidad Ratón", "Idioma", "Teclado USB",
	  "Ayuda", "Ratón USB<->Atari", "Reiniciar", "Joy1 D-sub<->USB", "Joy0 D-sub<->USB",
	  "Habilitar 270 MHz", "Habilitar 150 MHz", "Zona muerta joy.", "Volver", "Ajustes", "Depuración"},
    // IT
    { "Tastiera USB  ", "Mouse USB     ", "Joystick USB  ", "Mouse USB attivato", "Joy 0 attivato", "Velocità mouse", "Lingua", "Tastiera USB",
	  "Aiuto", "Mouse USB<->Atari", "Reset", "Joy1 D-sub<->USB", "Joy0 D-sub<->USB",
	  "Abilita 270 MHz", "Abilita 150 MHz", "Zona morta joy.", "Ritorno", "Impostazioni", "Debug"}
};

const char* get_translation(TranslationKey key, int lang_idx) {
    if (lang_idx < 0 || lang_idx >= 5) lang_idx = 0; // fallback EN
    if (key < 0 || key >= KEY_COUNT) return "";
    return translations[lang_idx][key];
}