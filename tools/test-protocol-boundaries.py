from pathlib import Path
import re
import bpk

root=Path(__file__).resolve().parents[1]
verbs=set(re.findall(r'"(DBG_[A-Z0-9_]+)"',(root/'components/ouse_installer/ouse_debug_transport.c').read_text(encoding='utf-8')))
for verb in verbs:
    assert 1<=len(verb)<=12,verb
    assert bpk.wire(1,2,verb,'-').startswith(b'OPK1 ')
for token,sequence,verb,payload in [(1,2,'DBG_SDRECOVER','-'),(1,2,'','-'),(1,2,'A B','-'),(1,2,'HELLO','a b'),(1,2,'HELLO',''),(1<<32,2,'HELLO','-'),(1,1<<32,'HELLO','-'),(1,2,'DATA','f'*1200)]:
    try:bpk.wire(token,sequence,verb,payload)
    except ValueError:pass
    else:raise AssertionError((token,sequence,verb,len(payload)))
print('PASS wire boundaries: every native debug command fits parser; invalid command/payload/IDs/oversized frames rejected locally instead of timing out')
