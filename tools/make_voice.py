#!/usr/bin/env python3
"""Builds the training coach voice clips: speech synthesis (espeak-ng) -> 8 kHz mono -> IMA ADPCM, one file per line and language.

Usage:  python3 tools/make_voice.py sdcard_files/race3d/voice          (pip install espeakng-loader numpy scipy)
Files:  <out>/<lang>/NN.adp   (NN = line number below).  Format: int16 LE first sample, uint8 step index, uint8 0, then 4-bit
        IMA ADPCM codes (low nibble first), 8000 Hz mono.  About 4 KB per second of speech.
The on-screen subtitles are the English keys in main/game/i18n.cpp (ASCII capitals); keep both in step with LINES.
"""
import ctypes, os, sys
import numpy as np
from scipy.signal import resample_poly

LINES = {   # number -> {lang: spoken text}
 0: {'en': "Hold A to accelerate.", 'fr': "Maintiens A pour accélérer.", 'de': "Halte A gedrückt, um zu beschleunigen.", 'es': "Mantén A para acelerar.", 'it': "Tieni premuto A per accelerare."},
 1: {'en': "Steer with left and right. Stay on the tarmac.", 'fr': "Dirige avec gauche et droite. Reste sur la route.", 'de': "Lenke mit links und rechts. Bleib auf der Straße.", 'es': "Gira con izquierda y derecha. Quédate en el asfalto.", 'it': "Sterza con sinistra e destra. Resta sull'asfalto."},
 2: {'en': "Sharp bend! Release A and brake with B.", 'fr': "Virage serré ! Lâche A et freine avec B.", 'de': "Scharfe Kurve! A loslassen und mit B bremsen.", 'es': "¡Curva cerrada! Suelta A y frena con B.", 'it': "Curva stretta! Rilascia A e frena con B."},
 3: {'en': "Hold A and the nitro button for a boost.", 'fr': "Maintiens A et le bouton nitro pour un coup de boost.", 'de': "Halte A und die Nitro-Taste für einen Schub.", 'es': "Mantén A y el botón de nitro para un impulso.", 'it': "Tieni premuti A e il tasto nitro per la spinta."},
 4: {'en': "Engine damaged. Take the pit lane on the right.", 'fr': "Moteur endommagé. Prends la voie des stands à droite.", 'de': "Motorschaden. Nimm die Boxengasse rechts.", 'es': "Motor dañado. Entra en el pit lane a la derecha.", 'it': "Motore danneggiato. Entra nella corsia box a destra."},
 5: {'en': "Stop in the bay. Wait for the green light.", 'fr': "Arrête-toi dans le box. Attends le feu vert.", 'de': "Halte in der Box. Warte auf das grüne Licht.", 'es': "Párate en el box. Espera la luz verde.", 'it': "Fermati nel box. Aspetta la luce verde."},
 6: {'en': "Lap two. Use the chevrons to anticipate bends.", 'fr': "Deuxième tour. Utilise les chevrons pour anticiper les virages.", 'de': "Zweite Runde. Nutze die Pfeile, um Kurven vorauszusehen.", 'es': "Segunda vuelta. Usa las flechas para anticipar las curvas.", 'it': "Secondo giro. Usa le frecce per anticipare le curve."},
 7: {'en': "Training complete. You are ready to race!", 'fr': "Entraînement terminé. Tu es prêt à courir !", 'de': "Training beendet. Du bist bereit fürs Rennen!", 'es': "Entrenamiento terminado. ¡Listo para correr!", 'it': "Allenamento finito. Sei pronto a correre!"},
}
VOICE = {'en': 'en-us', 'fr': 'fr', 'de': 'de', 'es': 'es', 'it': 'it'}
RATE_WPM = {'en': 150, 'fr': 150, 'de': 145, 'es': 150, 'it': 150}

STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767]
IDX = [-1,-1,-1,-1,2,4,6,8]

def synth(text, lang):
    import espeakng_loader as e
    lib = ctypes.CDLL(e.get_library_path())
    CB = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(ctypes.c_short), ctypes.c_int, ctypes.c_void_p)
    buf = []
    def cb(wav, n, ev):
        if wav and n > 0: buf.append(np.ctypeslib.as_array(wav, shape=(n,)).copy())
        return 0
    cbf = CB(cb)
    lib.espeak_Initialize.restype = ctypes.c_int
    sr = lib.espeak_Initialize(1, 0, e.get_data_path().encode(), 0)    # 1 = retrieval (callback)
    lib.espeak_SetSynthCallback(cbf)
    lib.espeak_SetVoiceByName(VOICE[lang].encode())
    lib.espeak_SetParameter(1, RATE_WPM[lang], 0)
    lib.espeak_SetParameter(3, 42, 0)                                  # pitch
    data = text.encode('utf-8') + b'\0'
    lib.espeak_Synth(data, len(data), 0, 1, 0, 1, None, None)
    lib.espeak_Synchronize()
    lib.espeak_Terminate()
    return sr, np.concatenate(buf).astype(np.float32) / 32768.0

def encode(pcm):
    x = np.clip(pcm * 32767, -32768, 32767).astype(np.int32)
    pred, idx = int(x[0]), 0
    out = bytearray([pred & 255, (pred >> 8) & 255, idx, 0])
    nib = []
    for s in x[1:]:
        step = STEP[idx]; diff = int(s) - pred; code = 0
        if diff < 0: code = 8; diff = -diff
        d = step >> 3
        if diff >= step: code |= 4; diff -= step; d += step
        if diff >= step >> 1: code |= 2; diff -= step >> 1; d += step >> 1
        if diff >= step >> 2: code |= 1; d += step >> 2
        pred += -d if code & 8 else d
        pred = max(-32768, min(32767, pred))
        idx = max(0, min(88, idx + IDX[code & 7]))
        nib.append(code)
    if len(nib) & 1: nib.append(0)
    for i in range(0, len(nib), 2): out.append(nib[i] | (nib[i + 1] << 4))
    return bytes(out)

def main(outdir):
    total = 0
    for lang in VOICE:
        os.makedirs('%s/%s' % (outdir, lang), exist_ok=True)
        for n, t in LINES.items():
            sr, pcm = synth(t[lang], lang)
            pcm = resample_poly(pcm, 8000, sr)
            nz = np.where(np.abs(pcm) > 0.02)[0]
            pcm = pcm[max(0, nz[0] - 80):nz[-1] + 200]                    # trim silence
            pcm = pcm / max(1e-6, np.abs(pcm).max()) * 0.92
            blob = encode(pcm)
            open('%s/%s/%02d.adp' % (outdir, lang, n), 'wb').write(blob)
            total += len(blob)
            print(lang, n, '%.2fs' % (len(pcm) / 8000.0), len(blob), 'bytes')
    print('total', total, 'bytes')

if __name__ == '__main__':
    main(sys.argv[1])
