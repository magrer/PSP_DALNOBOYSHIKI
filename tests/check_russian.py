#!/usr/bin/env python3
"""Verify all visible strings are localized, font coverage, and save path compatibility."""
from pathlib import Path
import re
root=Path(__file__).resolve().parent.parent
r=(root/'src/render_psp.c').read_text('utf-8')
s=(root/'src/sim.c').read_text('utf-8')
f=(root/'src/font.h').read_text('utf-8')
m=(root/'Makefile').read_text('utf-8')
expected=['ДАЛЬНИЙ РЕЙС', 'ПАУЗА','КВАДРАТ','ТЯГАЧ СМЕНЁН','РЕЙС ВЫПОЛНЕН','ПОГРУЗКА','ВЫГРУЗКА','ПОВЕРНИ ВЛЕВО','ПОВЕРНИ ВПРАВО','НЕ ХВАТАЕТ ДЕНЕГ','СТАРТ', 'ДОСТАВЬ ГРУЗ']
assert all(x in r for x in expected)
assert all(x in s for x in ['СЕВЕР 18','ТАЙГА 26','АТЛАНТ 34'])
assert 'ДАЛЬНИЙ РЕЙС' in m
for old in ['PAUSED','FREIGHT CONTRACT','REFUEL','COLLECT CARGO','DELIVER CARGO','GPS:', 'THROTTLE','SQUARE :','ANALOG :','ROAD HAUL']:
 assert old not in r, f'untranslated game string: {old}'
font_data=f.split('rh_font_ru[66][9]',1)[1].split('};',1)[0]
glyphs=re.findall(r'\{((?:\s*0x[0-9A-F]{2}\s*,?){9})\}',font_data)
assert len(glyphs)==66, f'expected 66 Cyrillic glyphs, got {len(glyphs)}'
assert 'ms0:/PSP/GAME/ROADHAUL/SAVE.DAT' in (root/'src/main_psp.c').read_text(), 'must preserve existing save compatibility'
print('PASS: all 66 Cyrillic glyphs, Russian text coverage, Russian title, compatible save paths')
