"""Embed the translations of the messages Orbit's service shows on the console itself.

backend/notify.c marks them MESSAGE("..."); their translations live in i18n/<code>.json with
every other interface string. A message without a translation stays in English."""
import json, re
from pathlib import Path
root = Path(__file__).resolve().parent.parent
languages = sorted(p.stem for p in (root / 'i18n').glob('*.json') if p.stem != 'en')
source = (root / 'backend/notify.c').read_text()
messages = re.findall(r'MESSAGE\("((?:[^"\\]|\\.)*)"\)', source)
tables = {code: json.loads((root / 'i18n' / f'{code}.json').read_text()) for code in languages}

def c(text):
    """A C string literal: printable ASCII as is, every other UTF-8 byte as an octal escape."""
    if text is None: return 'NULL'
    out = ''.join(chr(b) if 32 <= b < 127 and b not in (34, 92) else f'\\{b:03o}' for b in text.encode())
    return '"' + out + '"'

rows = []
for english in dict.fromkeys(messages):
    values = [tables[code].get(english) for code in languages]
    rows.append('{' + c(english) + ',{' + ','.join(c(v if isinstance(v, str) else None) for v in values) + '}}')
out = root / 'build/generated'
out.mkdir(parents=True, exist_ok=True)
(out / 'messages.h').write_text(
    '#ifndef ORBIT_MESSAGES_H\n#define ORBIT_MESSAGES_H\n#include <stddef.h>\n'
    f'#define ORBIT_MESSAGE_LANGUAGES {len(languages)}\n'
    'static const char *const orbit_message_languages[ORBIT_MESSAGE_LANGUAGES] = {' +
    ','.join(json.dumps(code) for code in languages) + '};\n'
    'static const struct { const char *english; const char *text[ORBIT_MESSAGE_LANGUAGES]; } orbit_messages[] = {' +
    ','.join(rows) + '};\n#endif\n')
print(f'Embedded {len(rows)} console messages in {len(languages)} languages')
