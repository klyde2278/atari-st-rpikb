#include "UserInterface.h"
#include <cstring>

const char* get_translation(const char* key, int lang_idx) {

	switch (lang_idx){
	
		case 0: // EN
			if (strcmp(key, "USB Keyboard") == 0)  return "USB Keyboard  ";
			if (strcmp(key, "USB Mouse") == 0)     return "USB Mouse     ";
			if (strcmp(key, "USB Joystick") == 0)  return "USB Joystick  ";
			if (strcmp(key, "Mouse enabled") == 0) return "Mouse enabled";
			if (strcmp(key, "Joy 0 enabled") == 0) return "Joy 0 enabled";
			if (strcmp(key, "Mouse speed") == 0)   return "Mouse speed";
			if (strcmp(key, "Language") == 0)      return "Language";
		break;

		case 1: // FR
		    if (strcmp(key, "USB Keyboard") == 0)  return "Clavier USB   ";
			if (strcmp(key, "USB Mouse") == 0)     return "Souris USB    ";
			if (strcmp(key, "USB Joystick") == 0)  return "Joystick USB  ";
			if (strcmp(key, "Mouse enabled") == 0) return "Souris activ(e";
			if (strcmp(key, "Joy 0 enabled") == 0) return "Joy 0 activ(";
			if (strcmp(key, "Mouse speed") == 0)   return "Vitesse souris";
			if (strcmp(key, "Language") == 0)      return "Langue";
		break;

		case 2: // DE
		    if (strcmp(key, "USB Keyboard") == 0)   return "USB-Tastatur  ";
			if (strcmp(key, "USB Mouse") == 0)      return "USB-Maus      ";
			if (strcmp(key, "USB Joystick") == 0)   return "USB-Joystick  ";
			if (strcmp(key, "Mouse enabled") == 0)  return "Maus aktivert";
			if (strcmp(key, "Joy 0 enabled") == 0)  return "Joy 0 aktivert";
			if (strcmp(key, "Mouse speed") == 0)    return "Maus-geschw.";
			if (strcmp(key, "Language") == 0)      return "Sprache";
		break;

		case 3: // SP
		    if (strcmp(key, "USB Keyboard") == 0)  return "Teclado USB   ";
			if (strcmp(key, "USB Mouse") == 0)     return "Rat)n USB     ";
			if (strcmp(key, "USB Joystick") == 0)  return "Joystick USB  ";
			if (strcmp(key, "Mouse enabled") == 0) return "Rat)n habilitado";
			if (strcmp(key, "Joy 0 enabled") == 0) return "Joy 0 habilitado";
			if (strcmp(key, "Mouse speed") == 0)   return "Velocidad Rat)n";
			if (strcmp(key, "Language") == 0)      return "Idioma";
			
		break;
		
		case 4: // IT
			if (strcmp(key, "USB Keyboard") == 0)  return "Tastiera USB  ";
			if (strcmp(key, "USB Mouse") == 0)     return "Mouse USB     ";
			if (strcmp(key, "USB Joystick") == 0)  return "Joystick USB  ";
			if (strcmp(key, "Mouse enabled") == 0) return "Mouse abilitato";
			if (strcmp(key, "Joy 0 enabled") == 0) return "Joy 0 abilitato";
			if (strcmp(key, "Mouse speed") == 0)   return "Velocit+ mouse";
			if (strcmp(key, "Language") == 0)      return "lingua";
		break;
	}

    return key;

}