// i18n.cpp - generated texts, see i18n.h
#include "i18n.h"
#include <string.h>

namespace i18n {
namespace {
struct Entry { const char* en; const char* t[COUNT - 1]; };
const Entry kTable[] = {
    {"NO WIFI NAME", {"PAS DE NOM WI-FI", "KEIN WLAN-NAME", "SIN NOMBRE WI-FI", "NESSUN NOME WI-FI"}},
    {"WIFI START", {"WI-FI NON LANCE", "WLAN-START FEHLT", "WI-FI NO INICIA", "WI-FI NON AVVIATO"}},
    {"WIFI: CANNOT JOIN", {"WI-FI: CONNEXION IMPOSSIBLE", "WLAN: KEINE VERBINDUNG", "WI-FI: SIN CONEXION", "WI-FI: NON CONNESSO"}},
    {"SERVER / TLS", {"SERVEUR / TLS", "SERVER / TLS", "SERVIDOR / TLS", "SERVER / TLS"}},
    {"SERVER", {"SERVEUR", "SERVER", "SERVIDOR", "SERVER"}},
    {"HANDSHAKE", {"ECHANGE REFUSE", "HANDSHAKE FEHLER", "FALLO DE ENLACE", "HANDSHAKE FALLITO"}},
    {"LINK LOST", {"LIEN PERDU", "VERBINDUNG WEG", "ENLACE PERDIDO", "LINK PERSO"}},
    {"NO MEMORY", {"MEMOIRE PLEINE", "KEIN SPEICHER", "SIN MEMORIA", "MEMORIA PIENA"}},
    {"TASK", {"TACHE IMPOSSIBLE", "TASK FEHLER", "TAREA FALLIDA", "TASK FALLITO"}},
    {"SERVER NOT FOUND", {"SERVEUR INTROUVABLE", "SERVER NICHT GEFUNDEN", "SERVIDOR NO ENCONTRADO", "SERVER NON TROVATO"}},
    {"CANNOT REACH THE SERVER", {"SERVEUR INJOIGNABLE", "SERVER NICHT ERREICHBAR", "SERVIDOR INALCANZABLE", "SERVER IRRAGGIUNGIBILE"}},
    {"NO TLS CERTIFICATES", {"PAS DE CERTIFICATS TLS", "KEINE TLS-ZERTIFIKATE", "SIN CERTIFICADOS TLS", "NESSUN CERTIFICATO TLS"}},
    {"TLS HANDSHAKE FAILED", {"ECHEC TLS", "TLS FEHLGESCHLAGEN", "FALLO TLS", "ERRORE TLS"}},
    {"SERVER REFUSED THE CONNECTION", {"CONNEXION REFUSEE", "VERBINDUNG ABGELEHNT", "CONEXION RECHAZADA", "CONNESSIONE RIFIUTATA"}},
    {"DISQUALIFIED", {"DISQUALIFIE", "DISQUALIFIZIERT", "DESCALIFICADO", "SQUALIFICATO"}},
    {"LEFT THE RACE", {"A QUITTE LA COURSE", "HAT DAS RENNEN VERLASSEN", "ABANDONO LA CARRERA", "HA LASCIATO LA GARA"}},
    {"HOLD A TO ACCELERATE.", {"MAINTIENS A POUR ACCELERER.", "HALTE A GEDRUCKT, UM ZU BESCHLEUNIGEN.", "MANTEN A PARA ACELERAR.", "TIENI PREMUTO A PER ACCELERARE."}},
    {"STEER WITH LEFT AND RIGHT. STAY ON THE TARMAC.", {"DIRIGE AVEC GAUCHE ET DROITE. RESTE SUR LA ROUTE.", "LENKE MIT LINKS UND RECHTS. BLEIB AUF DER STRASSE.", "GIRA CON IZQUIERDA Y DERECHA. QUEDATE EN EL ASFALTO.", "STERZA CON SINISTRA E DESTRA. RESTA SULL'ASFALTO."}},
    {"SHARP BEND! RELEASE A AND BRAKE WITH B.", {"VIRAGE SERRE! LACHE A ET FREINE AVEC B.", "SCHARFE KURVE! A LOSLASSEN UND MIT B BREMSEN.", "CURVA CERRADA! SUELTA A Y FRENA CON B.", "CURVA STRETTA! RILASCIA A E FRENA CON B."}},
    {"HOLD A AND THE NITRO BUTTON FOR A BOOST.", {"MAINTIENS A ET LE BOUTON NITRO POUR UN COUP DE BOOST.", "HALTE A UND DIE NITRO-TASTE FUR EINEN SCHUB.", "MANTEN A Y EL BOTON DE NITRO PARA UN IMPULSO.", "TIENI PREMUTI A E IL TASTO NITRO PER LA SPINTA."}},
    {"ENGINE DAMAGED. TAKE THE PIT LANE ON THE RIGHT.", {"MOTEUR ENDOMMAGE. PRENDS LA VOIE DES STANDS A DROITE.", "MOTORSCHADEN. NIMM DIE BOXENGASSE RECHTS.", "MOTOR DANADO. ENTRA EN EL PIT LANE A LA DERECHA.", "MOTORE DANNEGGIATO. ENTRA NELLA CORSIA BOX A DESTRA."}},
    {"STOP IN THE BAY. WAIT FOR THE GREEN LIGHT.", {"ARRETE-TOI DANS LE BOX. ATTENDS LE FEU VERT.", "HALTE IN DER BOX. WARTE AUF DAS GRUNE LICHT.", "PARATE EN EL BOX. ESPERA LA LUZ VERDE.", "FERMATI NEL BOX. ASPETTA LA LUCE VERDE."}},
    {"LAP TWO. USE THE CHEVRONS TO ANTICIPATE BENDS.", {"DEUXIEME TOUR. UTILISE LES CHEVRONS POUR ANTICIPER LES VIRAGES.", "ZWEITE RUNDE. NUTZE DIE PFEILE, UM KURVEN VORAUSZUSEHEN.", "SEGUNDA VUELTA. USA LAS FLECHAS PARA ANTICIPAR LAS CURVAS.", "SECONDO GIRO. USA LE FRECCE PER ANTICIPARE LE CURVE."}},
    {"TRAINING COMPLETE. YOU ARE READY TO RACE!", {"ENTRAINEMENT TERMINE. TU ES PRET A COURIR!", "TRAINING BEENDET. DU BIST BEREIT FURS RENNEN!", "ENTRENAMIENTO TERMINADO. LISTO PARA CORRER!", "ALLENAMENTO FINITO. SEI PRONTO A CORRERE!"}},
    {"TYRES", {"PNEUS", "REIFEN", "RUEDAS", "GOMME"}},
    {"TRAINING", {"ENTRAINEMENT", "TRAINING", "ENTRENAMIENTO", "ALLENAMENTO"}},
    {"GUIDED LESSONS + VOICE", {"LECONS GUIDEES + VOIX", "LEKTIONEN + STIMME", "LECCIONES + VOZ", "LEZIONI + VOCE"}},
    {"TRACK", {"CIRCUIT", "STRECKE", "CIRCUITO", "PISTA"}},
    {"DIFFICULTY", {"DIFFICULTE", "SCHWIERIGKEIT", "DIFICULTAD", "DIFFICOLTA"}},
    {"LAPS", {"TOURS", "RUNDEN", "VUELTAS", "GIRI"}},
    {"MUSIC", {"MUSIQUE", "MUSIK", "MUSICA", "MUSICA"}},
    {"LANGUAGE", {"LANGUE", "SPRACHE", "IDIOMA", "LINGUA"}},
    {"PLAYER", {"PILOTE", "FAHRER", "PILOTO", "PILOTA"}},
    {"START RACE", {"DEMARRER", "RENNEN START", "EMPEZAR", "VIA"}},
    {"ONLINE RACE (WI-FI)", {"COURSE EN LIGNE (WI-FI)", "ONLINE-RENNEN (WLAN)", "CARRERA ONLINE (WI-FI)", "GARA ONLINE (WI-FI)"}},
    {"OFF", {"NON", "AUS", "NO", "NO"}},
    {"EASY", {"FACILE", "LEICHT", "FACIL", "FACILE"}},
    {"NORMAL", {"NORMAL", "NORMAL", "NORMAL", "NORMALE"}},
    {"HARD", {"DIFFICILE", "SCHWER", "DIFICIL", "DIFFICILE"}},
    {"EXPERT", {"EXPERT", "PROFI", "EXPERTO", "ESPERTO"}},
    {"Relaxed rivals who make mistakes", {"Rivaux detendus qui font des erreurs", "Entspannte Gegner mit Fehlern", "Rivales relajados que fallan", "Rivali rilassati che sbagliano"}},
    {"A balanced field, a fair fight", {"Un plateau equilibre, duel loyal", "Ausgeglichenes Feld, fairer Kampf", "Parrilla equilibrada, lucha justa", "Gara equilibrata e leale"}},
    {"Fast and sharp. Use your nitro!", {"Rapides et precis. Utilise le nitro !", "Schnell und scharf. Nutze Nitro!", "Rapidos y afilados. Usa el nitro!", "Veloci e precisi. Usa il nitro!"}},
    {"Pro drivers. Perfect lines only.", {"Des pros. Trajectoires parfaites.", "Profis. Nur perfekte Linien.", "Profesionales. Solo trazadas limpias.", "Professionisti. Traiettorie perfette."}},
    {"LAP RECORD", {"RECORD TOUR", "RUNDENREKORD", "RECORD VUELTA", "RECORD GIRO"}},
    {"BEST", {"MEILLEUR", "BESTE", "MEJOR", "MIGLIORE"}},
    {"TIME", {"TEMPS", "ZEIT", "TIEMPO", "TEMPO"}},
    {"LAP", {"TOUR", "RUNDE", "VUELTA", "GIRO"}},
    {"KM/H", {"KM/H", "KM/H", "KM/H", "KM/H"}},
    {"N2O", {"N2O", "N2O", "N2O", "N2O"}},
    {"ENG", {"MOT", "MOT", "MOT", "MOT"}},
    {"GET READY", {"PREPAREZ-VOUS", "BEREIT MACHEN", "PREPARATE", "PRONTI"}},
    {"GO!", {"PARTEZ !", "LOS!", "YA!", "VIA!"}},
    {"ENGINE DAMAGED", {"MOTEUR ABIME", "MOTOR BESCHAEDIGT", "MOTOR DANADO", "MOTORE DANNEGGIATO"}},
    {"ENGINE FAILING!", {"MOTEUR EN PANNE !", "MOTOR VERSAGT!", "MOTOR FALLANDO!", "MOTORE IN AVARIA!"}},
    {"PIT STOP", {"STAND", "BOXENSTOPP", "PARADA", "PIT STOP"}},
    {"ENGINE REPAIRED", {"MOTEUR REPARE", "MOTOR REPARIERT", "MOTOR REPARADO", "MOTORE RIPARATO"}},
    {"TYRES & TRACKING OK", {"PNEUS ET TRAIN OK", "REIFEN & SPUR OK", "RUEDAS Y TREN OK", "GOMME E ASSETTO OK"}},
    {"PIT LANE - 85 KM/H", {"VOIE DES STANDS 85", "BOXENGASSE 85", "PIT LANE 85", "CORSIA BOX 85"}},
    {"N2O EXPRESS", {"N2O EXPRESS", "N2O EXPRESS", "N2O EXPRES", "N2O EXPRESS"}},
    {"BRAKE BEFORE BENDS", {"FREINE AVANT VIRAGE", "VOR KURVEN BREMSEN", "FRENA ANTES DE CURVA", "FRENA PRIMA CURVA"}},
    {"GRASS SLOWS YOU DOWN", {"L'HERBE TE RALENTIT", "GRAS BREMST DICH AB", "LA HIERBA TE FRENA", "L'ERBA TI RALLENTA"}},
    {"TYRES - STOP", {"PNEUS - STOP", "REIFEN - STOPP", "RUEDAS - PARA", "GOMME - FERMA"}},
    {"ENGINE GARAGE - STOP", {"GARAGE MOTEUR - STOP", "MOTORWERKSTATT - HALT", "TALLER MOTOR - PARA", "OFFICINA - FERMATI"}},
    {"GO! GO! GO!", {"ALLEZ ! ALLEZ !", "LOS! LOS! LOS!", "VAMOS! VAMOS!", "VIA! VIA! VIA!"}},
    {"PIT LANE ON THE RIGHT", {"STANDS A DROITE", "BOXEN RECHTS", "BOXES A LA DERECHA", "BOX A DESTRA"}},
    {"ENGINE DEAD", {"MOTEUR MORT", "MOTOR TOT", "MOTOR MUERTO", "MOTORE KO"}},
    {"WINNER!", {"VAINQUEUR !", "SIEGER!", "GANADOR!", "VINCITORE!"}},
    {"FINISH!", {"ARRIVEE !", "ZIEL!", "META!", "TRAGUARDO!"}},
    {"FINAL LAP", {"DERNIER TOUR", "LETZTE RUNDE", "ULTIMA VUELTA", "ULTIMO GIRO"}},
    {"BEST LAP!", {"MEILLEUR TOUR !", "BESTE RUNDE!", "MEJOR VUELTA!", "GIRO MIGLIORE!"}},
    {"CONNECTION LOST", {"CONNEXION PERDUE", "VERBINDUNG WEG", "CONEXION PERDIDA", "CONNESSIONE PERSA"}},
    {"LEFT THE RACE", {"A QUITTE LA COURSE", "HAT AUFGEGEBEN", "ABANDONO", "SI E RITIRATO"}},
    {"DISQUALIFIED", {"DISQUALIFIE", "DISQUALIFIZIERT", "DESCALIFICADO", "SQUALIFICATO"}},
    {"PAUSED", {"PAUSE", "PAUSE", "PAUSA", "PAUSA"}},
    {"RESUME", {"REPRENDRE", "WEITER", "CONTINUAR", "RIPRENDI"}},
    {"RESTART", {"RECOMMENCER", "NEUSTART", "REINICIAR", "RICOMINCIA"}},
    {"QUIT TO MENU", {"QUITTER AU MENU", "ZUM MENUE", "SALIR AL MENU", "ESCI AL MENU"}},
    {"(LIVE RACE)", {"(COURSE EN DIRECT)", "(LIVE-RENNEN)", "(CARRERA EN VIVO)", "(GARA IN DIRETTA)"}},
    {"LEAVE THE RACE", {"ABANDONNER", "AUFGEBEN", "ABANDONAR", "RITIRATI"}},
    {"POSITION", {"POSITION", "PLATZ", "PUESTO", "POSIZIONE"}},
    {"RACE RESULTS", {"RESULTATS", "ERGEBNISSE", "RESULTADOS", "RISULTATI"}},
    {"POS", {"POS", "PL", "POS", "POS"}},
    {"DRIVER", {"PILOTE", "FAHRER", "PILOTO", "PILOTA"}},
    {"BEST LAP", {"MEILL.", "BESTE", "MEJOR", "MIGLIORE"}},
    {"DNF", {"ABD", "DNF", "ABA", "RIT"}},
    {"YOU FINISHED", {"VOUS ARRIVEZ", "DU BIST", "ACABAS", "ARRIVI"}},
    {"ENGINE FAILURE", {"PANNE MOTEUR", "MOTORSCHADEN", "AVERIA DE MOTOR", "GUASTO AL MOTORE"}},
    {"NEW RACE RECORD!", {"NOUVEAU RECORD !", "NEUER REKORD!", "NUEVO RECORD!", "NUOVO RECORD!"}},
    {"NEW LAP RECORD!", {"RECORD DU TOUR !", "NEUER RUNDENREKORD!", "RECORD DE VUELTA!", "RECORD DEL GIRO!"}},
    {"A: RACE AGAIN   B: MENU", {"A: REJOUER   B: MENU", "A: NOCHMAL   B: MENUE", "A: OTRA VEZ   B: MENU", "A: ANCORA   B: MENU"}},
    {"A: BACK TO LOBBY   B: LEAVE", {"A: SALON   B: QUITTER", "A: LOBBY   B: VERLASSEN", "A: SALA   B: SALIR", "A: LOBBY   B: ESCI"}},
    {"ONLINE RACE", {"COURSE EN LIGNE", "ONLINE-RENNEN", "CARRERA ONLINE", "GARA ONLINE"}},
    {"SETTINGS FILE NOT FOUND", {"FICHIER DE REGLAGES ABSENT", "EINSTELLUNGSDATEI FEHLT", "FALTA EL ARCHIVO DE AJUSTES", "FILE IMPOSTAZIONI MANCANTE"}},
    {"NO WI-FI NAME IN THE FILE", {"PAS DE NOM WI-FI DANS LE FICHIER", "KEIN WLAN-NAME IN DER DATEI", "SIN NOMBRE WI-FI EN EL ARCHIVO", "NESSUN NOME WI-FI NEL FILE"}},
    {"Create this text file on the SD card:", {"Creez ce fichier sur la carte SD :", "Lege diese Datei auf der SD-Karte an:", "Crea este archivo en la tarjeta SD:", "Crea questo file di testo sulla SD:"}},
    {"The public race server is already filled in.", {"Le serveur public est deja renseigne.", "Oeffentlicher Server ist voreingestellt.", "El servidor publico ya esta indicado.", "Il server pubblico e gia impostato."}},
    {"Friends use the same room name to race together.", {"Les amis prennent le meme nom de salle.", "Freunde nutzen denselben Raumnamen.", "Los amigos usan el mismo nombre sala.", "Gli amici usano lo stesso nome stanza."}},
    {"See online.example.txt in the sdcard pack.", {"Voir online.example.txt (pack sdcard).", "Siehe online.example.txt im SD-Paket.", "Mira online.example.txt en el pack SD.", "Vedi online.example.txt (pacchetto SD)."}},
    {"B: BACK", {"B: RETOUR", "B: ZURUECK", "B: ATRAS", "B: INDIETRO"}},
    {"JOINING WI-FI...", {"CONNEXION WI-FI...", "WLAN VERBINDEN...", "CONECTANDO WI-FI...", "CONNESSIONE WI-FI..."}},
    {"CONNECTING TO THE SERVER...", {"CONNEXION AU SERVEUR...", "VERBINDE MIT SERVER...", "CONECTANDO AL SERVIDOR...", "CONNESSIONE AL SERVER..."}},
    {"The free server may need a minute to wake up.", {"Le serveur gratuit met 1 min a se reveiller.", "Der Gratis-Server braucht evtl. 1 Min.", "El servidor gratis tarda hasta 1 min.", "Il server gratuito ci mette fino a 1 min."}},
    {"ENTERING THE ROOM...", {"ENTREE DANS LA SALLE...", "BETRETE DEN RAUM...", "ENTRANDO EN LA SALA...", "ENTRO NELLA STANZA..."}},
    {"ROOM", {"SALLE", "RAUM", "SALA", "STANZA"}},
    {"YOU ARE THE HOST", {"VOUS ETES L'HOTE", "DU BIST HOST", "ERES EL ANFITRION", "SEI L'HOST"}},
    {"WAITING FOR THE HOST", {"ATTENTE DE L'HOTE", "WARTE AUF HOST", "ESPERANDO AL ANFITRION", "ATTESA DELL'HOST"}},
    {"ERROR", {"ERREUR", "FEHLER", "ERROR", "ERRORE"}},
    {"YOU", {"VOUS", "DU", "TU", "TU"}},
    {"YOU (HOST)", {"VOUS (HOTE)", "DU (HOST)", "TU (ANFITRION)", "TU (HOST)"}},
    {"COMPUTER", {"ORDI", "CPU", "CPU", "CPU"}},
    {"RACE IN PROGRESS...", {"COURSE EN COURS...", "RENNEN LAEUFT...", "CARRERA EN CURSO...", "GARA IN CORSO..."}},
    {"A: START RACE   B: LEAVE", {"A: LANCER   B: QUITTER", "A: START   B: VERLASSEN", "A: EMPEZAR   B: SALIR", "A: VIA   B: ESCI"}},
    {"LEFT/RIGHT: TRACK   UP/DOWN: LAPS", {"G/D: CIRCUIT  H/B: TOURS", "LINKS/RECHTS: STRECKE  AUF/AB: RUNDEN", "IZQ/DER: CIRCUITO  ARR/ABA: VUELTAS", "SX/DX: PISTA  SU/GIU: GIRI"}},
    {"WAITING FOR THE HOST...", {"ATTENTE DE L'HOTE...", "WARTE AUF HOST...", "ESPERANDO AL ANFITRION...", "ATTESA DELL'HOST..."}},
    {"B: LEAVE", {"B: QUITTER", "B: VERLASSEN", "B: SALIR", "B: ESCI"}},
    {"PLAYER NAME", {"NOM DU PILOTE", "FAHRERNAME", "NOMBRE DEL PILOTO", "NOME DEL PILOTA"}},
    {"A: ADD   B: DELETE   RUN: OK", {"A: AJOUTER  B: EFFACER  RUN: OK", "A: EINFUEGEN  B: LOESCHEN  RUN: OK", "A: ANADIR  B: BORRAR  RUN: OK", "A: AGGIUNGI  B: CANCELLA  RUN: OK"}},
    {"OK", {"OK", "OK", "OK", "OK"}},
    {"DEL", {"SUPPR", "LOESCH", "BORRAR", "CANC"}},
};
int g_lang = EN;
}

void setCode(const char* c) {
    g_lang = EN;
    if (!c) return;
    for (int i = 1; i < COUNT; ++i) if (c[0] == code(i)[0] && c[1] == code(i)[1]) g_lang = i;
}
int language() { return g_lang; }
const char* code(int l) { static const char* const k[COUNT] = { "en", "fr", "de", "es", "it" }; return k[l < 0 || l >= COUNT ? 0 : l]; }
const char* nativeName(int l) { static const char* const k[COUNT] = { "ENGLISH", "FRANCAIS", "DEUTSCH", "ESPANOL", "ITALIANO" }; return k[l < 0 || l >= COUNT ? 0 : l]; }
const char* tr(const char* s) {
    if (g_lang == EN || !s) return s;
    for (const Entry& e : kTable) if (e.en == s || !strcmp(e.en, s)) return e.t[g_lang - 1];
    return s;
}
const char* ordinal(int p) {
    static const char* const en[] = { "TH", "ST", "ND", "RD" };
    switch (g_lang) {
    case FR: return p == 1 ? "ER" : "E";
    case DE: return ".";
    case ES: case IT: return "o";
    default: return p >= 1 && p <= 3 ? en[p] : en[0];
    }
}
} // namespace i18n
