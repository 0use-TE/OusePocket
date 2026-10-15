"""Keep header and content offsets consistent without same-asset references.

Brookesia resolves each constant asset before merging it, so an expression in
that asset cannot reference a sibling constant declared by the same asset.
"""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
for path in (root / 'components/brookesia_app_settings/package/res/constants').glob('*.json'):
    doc = json.loads(path.read_text(encoding='utf-8'))
    layout = doc['data']['settings']['layout']
    header = int(layout['headerHeight'].removesuffix('dp'))
    layout['pageY'] = f'{header + 8}dp'
    layout['pageHeight'] = '${expr(${env.heightDp} - ' + str(header + 16) + 'dp)}'
    path.write_text(json.dumps(doc,ensure_ascii=False,indent=4)+'\n',encoding='utf-8')
