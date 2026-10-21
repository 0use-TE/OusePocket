"""Assemble the platformer engine and runtime adapter into a standalone BPK."""
from pathlib import Path
from bpk import pack
import runpy

ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'apps/跳跳庄园'
runpy.run_path(str(ROOT/'tools/build-pixel-levels.py'))['build']()
runpy.run_path(str(ROOT/'tools/build-manor-audio.py'))['build']()
(APP/'app.js').write_text((ROOT/'tools/app-shared/game-runtime.js').read_text(encoding='utf-8')+'\n'+'\n'.join((APP/name).read_text(encoding='utf-8') for name in ['engine.js','audio.js','menu.js','game.js']),encoding='utf-8')
pack(APP,ROOT/'dist/apps/跳跳庄园.bpk')
