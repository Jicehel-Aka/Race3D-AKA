// i18n.h - translations of every text of the game (English is the key; EN, FR, DE, ES, IT). ASCII only: the font has no accents.
#pragma once

namespace i18n {
enum Lang { EN = 0, FR, DE, ES, IT, COUNT };
void        setCode(const char* code);     // "en" "fr" "de" "es" "it" (anything else = English)
int         language();
const char* code(int lang);                // two-letter code
const char* nativeName(int lang);          // name of the language in itself, for the menu
const char* tr(const char* english);       // translated text (the English text itself when there is no entry)
const char* ordinal(int place);            // ST ND RD TH / ER E / ...
} // namespace i18n

#define TR(s) i18n::tr(s)
