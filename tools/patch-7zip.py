"""Reproducible PS5 compatibility patch for the pinned decoder source."""
from pathlib import Path
root = Path(__file__).resolve().parents[1] / '.deps/7zip'
p = root / 'C/AesOpt.c'
source = p.read_text()
marker = '#ifdef USE_INTEL_AES\n'
patch = '''/* PS5's Zen 2 CPU has AES-NI but no VAES; its toolchain omits those intrinsics. */
#ifdef ORBIT7ZIP_PS5
#undef USE_INTEL_VAES
#define Z7_USE_VAES_HW_STUB
#endif

'''
if patch not in source:
    assert marker in source
    p.chmod(p.stat().st_mode | 0o200)
    p.write_text(source.replace(marker, patch + marker, 1))

# The PS5 SDK also omits the newer SHA-512 vector extension. Use upstream's
# software stub for that optional acceleration path (not for AES or SHA-256).
p = root / 'C/Sha512Opt.c'
source = p.read_text()
marker = '#ifdef USE_HW_SHA\n'
patch = '''#ifdef ORBIT7ZIP_PS5
#undef USE_HW_SHA
#define Z7_USE_HW_SHA_STUB
#endif

'''
if patch not in source:
    assert marker in source
    p.chmod(p.stat().st_mode | 0o200)
    p.write_text(source.replace(marker, patch + marker, 1))
