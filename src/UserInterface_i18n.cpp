#include "UserInterface.h"
#include "UserInterface_i18n.h"
#include <cstring>

const char* languages[] = { "EN", "FR", "DE", "SP", "IT" };
const int NUM_LANGUAGES = sizeof(languages) / sizeof(languages[0]);

static const char* translations[][KEY_COUNT] = {
    // EN
    { "USB Keyboard  ", "USB Mouse     ", "USB Joystick  ", "Mouse enabled", "Joy 0 enabled", "Mouse speed", "Language" },
    // FR
    { "Clavier USB   ", "Souris USB    ", "Joystick USB  ", "Souris activ(e", "Joy 0 activ(", "Vitesse souris", "Langue" },
    // DE
    { "USB-Tastatur  ", "USB-Maus      ", "USB-Joystick  ", "Maus aktiviert", "Joy 0 aktivert", "Maus-geschw.", "Sprache" },
    // SP
    { "Teclado USB   ", "Rat)n USB     ", "Joystick USB  ", "Rat)n habilitado", "Joy 0 habilitado", "Velocidad Rat)n", "Idioma" },
    // IT
    { "Tastiera USB  ", "Mouse USB     ", "Joystick USB  ", "Mouse abilitato", "Joy 0 abilitato", "Velocit+ mouse", "Lingua" }
};

const char* get_translation(TranslationKey key, int lang_idx) {
    if (lang_idx < 0 || lang_idx >= 5) lang_idx = 0; // fallback EN
    if (key < 0 || key >= KEY_COUNT) return "";
    return translations[lang_idx][key];
}