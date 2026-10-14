# -*- coding: utf-8 -*-
"""ElevenLabs 에서 받은 음성 파일 후처리 (prompts.json direction.post).

  1. 앞 0.05초, 뒤 0.25초 무음만 남기고 자른다(앞 5 ms, 뒤 20 ms 페이드).
  2. 말 속도: 앞뒤 무음을 뺀 길이로 약 4음절/초(한성윤 기준 10/12)가 되도록 음높이를 바꾸지 않고 늘인다
     (ffmpeg atempo). 최대 x0.7 까지만 늘이고 빠르게 하지는 않는다. 음절 수는 prompts.json 의 say(한글 음절).
  3. 음량 -20 LUFS, 실제 첨두 -1.5 dBTP 제한(리미터).
  4. MP3 44.1 kHz 모노 128 kbps, 메타데이터 없음(ElevenLabs 출처 정보 C2PA 포함), 길이 머리(Xing)는 넣는다.

개발 PC 도구다(Jetson 실행 코드가 아님). 필요: numpy, soundfile(libsndfile 1.1 이상, MP3 읽기), imageio-ffmpeg.
  pip install numpy soundfile imageio-ffmpeg
사용:
  python voice_process.py 받은폴더 ui/public/voice ui/src/voice/prompts.json
원본은 저장소에 올리지 않는다. 결과 요약은 표준 출력에만 낸다(저장소에 남길 수치는 docs/voice_prompts.md 에 옮긴다).
"""
import json
import os
import re
import subprocess
import sys
import tempfile

import imageio_ffmpeg
import numpy as np
import soundfile as sf

FF = imageio_ffmpeg.get_ffmpeg_exe()
TARGET_RATE = 4.0           # 음절/초 (앞뒤 무음 제외, 문장 안 쉼 포함)
MIN_FACTOR = 0.70           # 이보다 더 늘이지 않는다
TARGET_LUFS = -20.0
TP_LIMIT_DB = -1.5
SILENCE_DB = -45.0          # 10 ms 창 RMS 가 이보다 크면 소리로 본다
LEAD, TAIL = 0.050, 0.250


def span(mono, sr):
    """소리가 처음 나는 표본과 마지막으로 나는 표본(10 ms 창, SILENCE_DB)"""
    fr = int(0.010 * sr)
    n = len(mono) // fr
    db = 20 * np.log10(np.sqrt((mono[:n * fr].reshape(n, fr) ** 2).mean(axis=1) + 1e-20))
    on = np.where(db > SILENCE_DB)[0]
    if not len(on):
        raise ValueError('소리가 없음')
    return on[0] * fr, (on[-1] + 1) * fr


def loudness(path):
    e = subprocess.run([FF, '-hide_banner', '-nostats', '-i', path, '-af', 'ebur128=peak=true', '-f', 'null', '-'],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True).stderr
    return float(re.findall(r'I:\s+(-?[\d.]+) LUFS', e)[-1]), float(re.findall(r'Peak:\s+(-?[\d.]+) dBFS', e)[-1])


def read_mono(path):
    x, sr = sf.read(path, dtype='float64', always_2d=True)   # 길이는 실제 디코딩 기준(머리 정보 아님)
    return x.mean(axis=1), sr


def process(src, dst, factor):
    mono, sr = read_mono(src)
    a, b = span(mono, sr)
    a = max(0, a - int(LEAD * sr))
    b = min(len(mono), b + int(TAIL * sr))
    y = mono[a:b].copy()
    fi, fo = int(0.005 * sr), int(0.020 * sr)
    y[:fi] *= np.linspace(0, 1, fi)
    y[-fo:] *= np.linspace(1, 0, fo)
    td = tempfile.mkdtemp()
    try:
        w1, w2 = os.path.join(td, 'a.wav'), os.path.join(td, 'b.wav')
        sf.write(w1, y, sr, subtype='FLOAT')
        af = 'atempo=%.4f' % factor if abs(factor - 1) > 1e-6 else 'anull'
        subprocess.run([FF, '-hide_banner', '-loglevel', 'error', '-y', '-i', w1, '-af', af, '-c:a', 'pcm_f32le', w2], check=True)
        i_lufs, _ = loudness(w2)
        lim = 10 ** (TP_LIMIT_DB / 20)
        af2 = 'volume=%.3fdB,alimiter=limit=%.4f:attack=5:release=50:level=disabled:latency=1' % (TARGET_LUFS - i_lufs, lim)
        subprocess.run([FF, '-hide_banner', '-loglevel', 'error', '-y', '-i', w2, '-af', af2, '-ar', '44100', '-ac', '1',
                        '-c:a', 'libmp3lame', '-b:a', '128k', '-map_metadata', '-1', '-id3v2_version', '0', '-write_xing', '1', dst],
                       check=True)
    finally:
        for f in os.listdir(td):
            os.remove(os.path.join(td, f))
        os.rmdir(td)


def main(src_dir, out_dir, prompts_json):
    prompts = {p['id']: p for p in json.load(open(prompts_json, encoding='utf-8'))['prompts']}
    files = sorted(f for f in os.listdir(src_dir) if re.match(r'^V\d\d\.mp3$', f))
    unknown = [f for f in files if f[:-4] not in prompts]
    if unknown:
        sys.exit('prompts.json 에 없는 ID: ' + ', '.join(unknown))
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)
    print('%-4s %4s %7s %6s %7s %6s %6s' % ('ID', '음절', '받은속도', '배율', '처리속도', 'LUFS', 'TP'))
    for f in files:
        vid = f[:-4]
        syl = len(re.findall(u'[\uac00-\ud7a3]', prompts[vid]['say']))
        mono, sr = read_mono(os.path.join(src_dir, f))
        a, b = span(mono, sr)
        rate_in = syl / ((b - a) / float(sr))
        factor = round(max(MIN_FACTOR, min(1.0, TARGET_RATE / rate_in)), 3)
        dst = os.path.join(out_dir, f)
        process(os.path.join(src_dir, f), dst, factor)
        m2, sr2 = read_mono(dst)
        a2, b2 = span(m2, sr2)
        lufs, tp = loudness(dst)
        print('%-4s %4d %7.2f %6.3f %7.2f %6.1f %6.1f' % (vid, syl, rate_in, factor, syl / ((b2 - a2) / float(sr2)), lufs, tp))


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], sys.argv[3])
