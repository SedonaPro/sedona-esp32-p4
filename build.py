#!/usr/bin/env python3
"""Build Bclimate Sedona for ESP32-S3.

Sedona side (needs only Java and ../runtime):
  python3 build.py sedona      overlay home, bclimateEsp32 kit, natives,
                               kits.scode, app.sab, svm.conf -> build/
ESP32 simulator on this PC (Java + gcc, no ESP-IDF), see platform/sim:
  python3 build.py sim         build/sim/esp32sim + its data (refSize 8)
  python3 build.py simrun [--erase] [-- esp32sim options]
                               flash build/sim/data into build/sim/flash
                               (--erase, or when empty) and run it
Firmware side (needs ESP-IDF in the environment, see README_RU.md):
  python3 build.py fw   [--profile qemu|hw|tests]   idf.py build -> build/fw-<profile>
  python3 build.py qemu [--profile qemu|tests]      run it in Espressif QEMU
  python3 build.py all  [--profile ...]             sedona + fw
Profiles (firmware/sdkconfig.<profile> on top of sdkconfig.defaults):
  qemu   OpenCores Ethernet for QEMU, SOX on host udp/1876 (default)
  hw     Wi-Fi station for a real board (SSID: idf.py menuconfig)
  tests  runs tests.scode (sedona unit tests) instead of the platform

Modules (--modules default|all|none|name,name; default = all for the target):
@MODULES@

Kit manifests (--manifests all|zip|xml|none, default all): the manifests of
exactly the kits in kits.scode go to <data>/manifests (m.zip and
<kit>/<kit>-<checksum>.xml); bclimateEsp32::Esp32ManifestStore serves them over
SOX as m:m.zip and m:<kit>-<checksum>.xml, so MCS loads the schema itself.

The runtime sources are used as they are: build/home is a sedona.home made
of links to ../runtime (src, lib, kits, manifests) plus this repository as
/esp32, because sedonac takes native sources only from under sedona.home.
"""
import argparse
import base64
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
import copy
import xml.etree.ElementTree as ET
import zipfile

import modules as M

ROOT = Path(__file__).resolve().parent
RUNTIME = Path(os.environ.get('SEDONA_RUNTIME', ROOT / 'runtime')).resolve()
BUILD = ROOT / 'build'
HOME = BUILD / 'home'
NATIVE = BUILD / 'native'
DATA = BUILD / 'data'          # contents of the LittleFS partition
FW = ROOT / 'firmware'
DEV_PASSWORD = 'esp32-dev-only'


def run(args, **kwargs):
    args = list(map(str, args))
    print('+', shlex.join(args), flush=True)
    subprocess.run(args, check=True, **kwargs)


def sedonac(*args):
    run(['java', '-Dsedona.home=' + str(HOME), '-cp',
         os.pathsep.join(str(RUNTIME / 'lib' / n) for n in ('sedona.jar', 'sedonac.jar')),
         'sedonac.Main', *args])


def link(path, target):
    if path.is_symlink() or path.exists():
        if path.is_symlink() and path.resolve() == target.resolve():
            return
        path.unlink()
    path.symlink_to(target)


def overlay_home():
    """build/home: runtime links + /esp32 -> this repository."""
    if not (RUNTIME / 'lib/sedonac.jar').exists():
        run([sys.executable, RUNTIME / 'build.py', 'jars'])
    if not (RUNTIME / 'kits/sys').is_dir():
        run([sys.executable, RUNTIME / 'build.py', 'kits'])
    HOME.mkdir(parents=True, exist_ok=True)
    for name in ('lib', 'platforms'):
        link(HOME / name, RUNTIME / name)
    # App.startApp must not cache the component array while start callbacks add
    # components (LogMgr can resize/free it). Compile a private sys kit overlay.
    src = HOME / 'src'
    if src.is_symlink():
        src.unlink()
    src.mkdir(exist_ok=True)
    for sub in sorted((RUNTIME / 'src').iterdir()):
        if sub.name != 'sys':
            link(src / sub.name, sub)
    own_sys = src / 'sys'
    if own_sys.exists():
        shutil.rmtree(own_sys)
    shutil.copytree(RUNTIME / 'src/sys', own_sys)
    app = own_sys / 'App.sedona'
    text = app.read_text()
    for callback in ('loaded', 'start'):
        old = '    foreach (Component c : comps, compsLen)\n      if (c != null) c.%s()' % callback
        new = ('    for (int i = 0; i < compsLen; ++i)\n    {\n'
               '      Component c = comps[i]\n      if (c != null) c.%s()\n    }') % callback
        if old not in text:
            raise SystemExit('sys App lifecycle adapter: expected pattern missing')
        text = text.replace(old, new)
    app.write_text(text)
    link(HOME / 'esp32', ROOT)
    for name in ('kits', 'manifests'):
        own = HOME / name
        own.mkdir(exist_ok=True)
        for sub in sorted((RUNTIME / name).iterdir()):
            if sub.is_dir():
                if sub.name == 'sys':
                    dest = own / sub.name
                    if dest.is_symlink(): dest.unlink()
                    dest.mkdir(exist_ok=True)
                else:
                    link(own / sub.name, sub)


def platform_kit():
    sedonac(HOME / 'src/sys/kit.xml')
    sedonac(ROOT / 'platform/bclimateEsp32/kit.xml')


def platform_manifest(mods, outdir, refsize=None, plat_id=None):
    """platform/esp32-sedona.xml plus the native kits and sources of mods."""
    xml = ET.parse(ROOT / 'platform/esp32-sedona.xml')
    comp = xml.getroot().find('compile')
    if refsize:
        comp.set('refSize', str(refsize))
    if plat_id:
        xml.getroot().set('id', plat_id)
    last_kit = comp.findall('nativeKit')[-1]
    for name in mods:
        m = M.MODULES[name]
        for k in m.get('native_kits', []):
            el = ET.Element('nativeKit', depend=k + ' 1.2+')
            comp.insert(list(comp).index(last_kit) + 1, el)
            last_kit = el
        for src in m.get('sources', []):
            ET.SubElement(comp, 'nativeSource', path=src)
    path = outdir / 'platform.xml'
    xml.write(path, encoding='unicode')
    return path


def natives(manifest, out):
    """Stage native sources and nativetable.c."""
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    sedonac(manifest, '-outDir', out)
    # Keep shared runtime untouched; adapt its legacy socket ABI in staged copies.
    fixes = {
        'sys_FileStore_std.c': [('  syncfs(ifd);\n  sync();', '#if !defined(ESP_PLATFORM) || defined(ESP32SIM)\n  syncfs(ifd);\n  sync();\n#endif')],
        'inet_TcpSocket_std.c': [('int arglen = sizeof(int);', 'socklen_t arglen = sizeof(int);')],
        'inet_UdpSocket_std.c': [('int addrLen = sizeof(addr);', 'socklen_t addrLen = sizeof(addr);')],
        'inet_util_std.c': [('int* port, int* scope, int* flow', 'int32_t* port, int32_t* scope, int32_t* flow')],
        'inet_util_std.h': [('int* port, int* scope, int* flow', 'int32_t* port, int32_t* scope, int32_t* flow')],
    }
    for filename, replacements in fixes.items():
        path = out / filename
        source = path.read_text()
        for old, new in replacements:
            if old not in source:
                raise SystemExit('socket ABI adapter: expected pattern missing in ' + filename)
            source = source.replace(old, new)
        path.write_text(source)
    path = out / 'datetimeStd_DateTimeServiceStd.c'
    source = path.read_text()
    start = source.index('  #ifdef _WIN32', source.index('Cell datetimeStd_DateTimeServiceStd_doGetUtcOffset'))
    end = source.index('  #endif', start) + len('  #endif')
    # No timezone/daylight globals in picolibc. Derive current offset via C time API.
    source = source[:start] + """#if defined(ESP_PLATFORM) && !defined(ESP32SIM)
    struct tm utc;
    if (!lcltime || !gmtime_r(&now, &utc)) return zeroCell;
    utc.tm_isdst = lcltime->tm_isdst;
    result.ival = (int32_t)difftime(now, mktime(&utc));
#else
""" + source[start:end] + '\n#endif' + source[end:]
    path.write_text(source)
    path = out / 'svm_loop.c'
    source = path.read_text()
    source += """
// Snapshot read only by the VM task; no additional writer or locking.
int64_t svm_diag_metric(int which)
{
  switch(which) {
    case 6: return st.totalCycles;
    case 7: return st.periods ? st.sumNs / st.periods / 1000 : 0;
    case 8: return st.periods ? st.minNs / 1000 : 0;
    case 9: return st.periods ? st.maxNs / 1000 : 0;
    case 10: return st.totalOverruns;
    case 11: return st.skippedPeriods;
    default: return -1;
  }
}
"""
    path.write_text(source)
    print('staged %d C files in %s' % (len(list(out.glob('*.c'))), out))


def scode(source, out_name, mods, test=False, refsize=None, outdir=BUILD):
    xml = ET.parse(source)
    xml.getroot().set('test', 'true' if test else 'false')
    have = {d.get('on').split()[0] for d in xml.getroot().findall('depend')}
    for name in mods:
        for k in M.MODULES[name]['kits']:
            if k not in have:
                ET.SubElement(xml.getroot(), 'depend', on=k + ' 1.2+')
                have.add(k)
    if refsize:
        xml.getroot().set('refSize', str(refsize))
    if test:
        core = {'bclimateEsp32', 'sys', 'types', 'math', 'bclimateMath', 'datetime', 'func', 'logic'}
        for dep in xml.getroot().findall('depend'):
            dep.set('test', str(dep.get('on').split()[0] in core).lower())
    path = outdir / (out_name + '.xml')
    xml.write(path, encoding='unicode')
    sedonac(path)
    return path.with_suffix('.scode')


def scode_kits(path):
    """[(name, checksum, version)] of the kits linked into a .scode image."""
    b = Path(path).read_bytes()
    if struct.unpack_from('<I', b, 0)[0] != 0x5ED0BA07:
        raise SystemExit('%s: not a little-endian scode image' % path)
    block = b[6]

    def u2(off):
        return struct.unpack_from('<H', b, off)[0]

    def cstr(index):
        off = index * block
        return b[off:b.index(0, off)].decode()
    kits_at, count = u2(20) * block, b[22]
    out = []
    for i in range(count):
        k = u2(kits_at + 2 * i) * block
        name, version = cstr(u2(k + 2)), cstr(u2(k + 4))
        out.append((name, struct.unpack_from('<I', b, k + 8)[0], version))
    return out


MANIFEST_MODES = ('all', 'zip', 'xml', 'none')


def manifests(scode_path, data, mode='all'):
    """data/manifests: the kit manifests of exactly the kits in scode_path, served by
    bclimateEsp32::Esp32ManifestStore as m:m.zip and m:<kit>-<checksum>.xml so that
    a SOX client (MCS) with an empty cache can load the schema from the device."""
    if mode == 'none':
        return []
    out = data / 'manifests'
    if out.exists():
        shutil.rmtree(out)
    out.mkdir()
    parts, missing = [], []
    for name, chk, _ in scode_kits(scode_path):
        part = '%s-%08x' % (name, chk)
        src = HOME / 'manifests' / name / (part + '.xml')
        if not src.exists():
            missing.append(part)
            continue
        parts.append((name, part, src.read_bytes()))
    if missing:
        raise SystemExit('no manifest for kit(s) in %s: %s' % (scode_path, ', '.join(missing)))
    if mode in ('all', 'xml'):
        for name, part, xml in parts:
            (out / name).mkdir(exist_ok=True)
            (out / name / (part + '.xml')).write_bytes(xml)
    if mode in ('all', 'zip'):
        with zipfile.ZipFile(out / 'm.zip', 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for name, part, xml in parts:
                info = zipfile.ZipInfo('%s/%s.xml' % (name, part), (2020, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                z.writestr(info, xml)
    size = sum(f.stat().st_size for f in out.rglob('*') if f.is_file())
    print('manifests (%s): %d kits, %d bytes' % (mode, len(parts), size))
    return [p for _, p, _ in parts]


def app(password, mods, outdir=BUILD, source=None):
    """apps/esp32-demo.sax + apps/modules/<module>.xml for each module."""
    cred = base64.b64encode(hashlib.sha1(('admin:' + password).encode()).digest()).decode()
    source = source or os.environ.get('SEDONA_APP_SOURCE')
    xml = ET.parse(source) if source else ET.ElementTree(ET.fromstring((ROOT / 'apps/esp32-demo.sax').read_text()
                                       .replace('@ADMIN_CRED@', cred)))
    root = xml.getroot()
    schema, app_el, links = root.find('schema'), root.find('app'), root.find('links')
    # Rebind the saved app to the newly compiled kits; preserve components and values.
    if source:
        for kit in schema.findall('kit'):
            kit.attrib.pop('checksum', None)
    kits = {k.get('name') for k in schema.findall('kit')}
    next_id = 1 + max(int(c.get('id')) for c in app_el.iter('comp'))

    def find(path):
        el = app_el
        for n in [x for x in path.split('/') if x]:
            el = next(c for c in el.findall('comp') if c.get('name') == n)
        return el
    for name in mods:
        if source and name != 'controller':
            continue
        frag = ROOT / 'apps/modules' / (name + '.xml')
        if not frag.exists():
            continue
        f = ET.parse(frag).getroot()
        for k in f.findall('kit'):
            if k.get('name') not in kits:
                ET.SubElement(schema, 'kit', name=k.get('name'))
                kits.add(k.get('name'))
        for into in f.findall('into'):
            target = find(into.get('path', ''))
            def merge(parent, component):
                nonlocal next_id
                existing = next((x for x in parent.findall('comp') if x.get('name') == component.get('name')), None)
                if existing is not None:
                    if existing.get('type') != component.get('type'):
                        raise SystemExit('app merge: incompatible existing component ' + component.get('name'))
                    for child in component.findall('comp'):
                        merge(existing, child)
                    return
                added = copy.deepcopy(component)
                for x in added.iter('comp'):
                    x.set('id', str(next_id))
                    next_id += 1
                parent.append(added)
            for c in into.findall('comp'):
                merge(target, c)
        for ln in f.findall('link'):
            links.append(copy.deepcopy(ln))
    src = outdir / 'app.sax'
    xml.write(src, encoding='unicode')
    sedonac(src)
    return src.with_suffix('.sab')


def sedona(args):
    BUILD.mkdir(exist_ok=True)
    mods = M.select(args.modules, 'esp32p4' if args.profile.startswith('p4') else 'esp32')
    print('modules:', ', '.join(mods) or '-')
    overlay_home()
    platform_kit()
    plat_id = 'bclimate-esp32p4-${sedona.env.version}' if args.profile.startswith('p4') else None
    natives(platform_manifest(mods, BUILD, plat_id=plat_id), NATIVE)
    if DATA.exists():
        shutil.rmtree(DATA)
    DATA.mkdir()
    kits = scode(ROOT / 'scode/esp32-kits.xml', 'kits', mods)
    shutil.copy2(kits, DATA / 'kits.scode')
    password = os.environ.get('SEDONA_ADMIN_PASSWORD')
    if not password:
        password = DEV_PASSWORD
        print('NOTE: admin password is the development default %r '
              '(set SEDONA_ADMIN_PASSWORD)' % DEV_PASSWORD)
    shutil.copy2(app(password, mods, source=args.app_source), DATA / 'app.sab')
    shutil.copy2(ROOT / 'firmware/svm.conf', DATA / 'svm.conf')
    manifests(DATA / 'kits.scode', DATA, args.manifests)
    if args.tests:
        tests = scode(ROOT / 'scode/esp32-kits.xml', 'tests', mods, test=True)
        shutil.copy2(tests, DATA / 'tests.scode')
    for f in sorted(p for p in DATA.iterdir() if p.is_file()):
        print('  data/%-12s %7d bytes' % (f.name, f.stat().st_size))


SIM = BUILD / 'sim'
WRAPPED = ['fopen', 'rename', 'remove', 'unlink', 'stat', 'open', 'mkdir', 'chdir']


def sim(args):
    """Host build of the firmware's Sedona code with ESP-IDF shims."""
    SIM.mkdir(parents=True, exist_ok=True)
    mods = M.select(args.modules, 'sim')
    print('modules:', ', '.join(mods) or '-')
    (SIM / 'modules.txt').write_text('\n'.join(mods) + '\n')
    overlay_home()
    platform_kit()
    # the same natives as the firmware, staged for a 64-bit host (refSize 8)
    native = SIM / 'native'
    natives(platform_manifest(mods, SIM, refsize=8, plat_id='bclimate-esp32sim-1.2.29'), native)
    srcs = [p for p in sorted(native.glob('*.c')) if p.name not in ('svm_crash.c', 'svm_rt.c', 'main.c')]
    flags = ['-std=gnu99', '-O2', '-g', '-fno-strict-aliasing', '-fcommon', '-D_GNU_SOURCE',
             '-DSEDONA_LOCAL_H', '-DESP_PLATFORM=1', '-DESP32SIM=1', '-DSOCKET_FAMILY_INET=1',
             '-DPLAT_BUILD_VERSION="1.2.29-esp32sim"', '-DCOMPUTED_GOTO',
             '-I' + str(ROOT / 'platform/sim/include'), '-I' + str(native)]
    libs = []
    for name in mods:
        flags += M.MODULES[name].get('cflags', [])
        libs += M.MODULES[name].get('libs', [])
    objs = SIM / 'obj'
    if objs.exists():
        shutil.rmtree(objs)
    objs.mkdir()
    run(['gcc', *flags, '-Dmain=svm_main', '-c', native / 'main.c', '-o', objs / 'svm_main.o'])
    wraps = ['-Wl,--wrap=' + f for f in WRAPPED + ['malloc', 'calloc', 'realloc']]
    tmp = SIM / 'esp32sim.new'
    run(['gcc', *flags, *srcs, ROOT / 'platform/sim/esp32sim.c', objs / 'svm_main.o',
         '-o', tmp, *wraps, *libs, '-lm', '-pthread'])
    tmp.replace(SIM / 'esp32sim')
    data = SIM / 'data'
    if data.exists():
        shutil.rmtree(data)
    data.mkdir()
    shutil.copy2(scode(ROOT / 'scode/esp32-kits.xml', 'kits', mods, refsize=8, outdir=SIM),
                 data / 'kits.scode')
    shutil.copy2(scode(ROOT / 'scode/esp32-kits.xml', 'tests', mods, test=True, refsize=8, outdir=SIM),
                 data / 'tests.scode')
    password = os.environ.get('SEDONA_ADMIN_PASSWORD') or DEV_PASSWORD
    shutil.copy2(app(password, mods, SIM, source=args.app_source), data / 'app.sab')
    shutil.copy2(ROOT / 'firmware/svm.conf', data / 'svm.conf')
    manifests(data / 'kits.scode', data, args.manifests)
    print('BUILT', SIM / 'esp32sim')


def simrun(erase, extra):
    flash = SIM / 'flash'
    if erase and flash.exists():
        shutil.rmtree(flash)
    if not flash.exists():
        shutil.copytree(SIM / 'data', flash)
        print('flashed', flash)
    os.execv(str(SIM / 'esp32sim'), [str(SIM / 'esp32sim'), '--flash', str(flash), *extra])


def idf(profile, *args):
    if not os.environ.get('IDF_PATH'):
        sys.exit('ESP-IDF is not in the environment: . ~/esp/esp-idf-v6.0.3/export.sh')
    out = BUILD / ('fw-' + profile)
    run(['idf.py', '-C', FW, '-B', out,
         '-DIDF_TARGET=' + ('esp32p4' if profile.startswith('p4') else 'esp32s3'),
         '-DSDKCONFIG=' + str(out / 'sdkconfig'),
         '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.' + profile, *args])


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('target', choices=['sedona', 'sim', 'simrun', 'fw', 'qemu', 'all'])
    p.add_argument('--profile', choices=['qemu', 'hw', 'tests', 'p4', 'p4-tests'], default='p4')
    p.add_argument('--app-source', help='preserve existing SAX app; append controller fragment only')
    p.add_argument('--tests', action='store_true', help='also put tests.scode on the data partition')
    p.add_argument('--qemu-args', default='', help='extra QEMU arguments')
    p.add_argument('--erase', action='store_true', help='simrun: start from a freshly flashed file system')
    p.add_argument('--modules', default='default', help='optional modules, see above')
    p.add_argument('--manifests', choices=MANIFEST_MODES, default='all',
                   help='kit manifests on the data partition for SOX clients (m:m.zip, '
                        'm:<kit>-<checksum>.xml): all (default), zip, xml or none')
    argv = sys.argv[1:]
    extra = []
    if '--' in argv:
        extra = argv[argv.index('--') + 1:]
        argv = argv[:argv.index('--')]
    args = p.parse_args(argv)
    if args.profile in ('tests', 'p4-tests'):
        args.tests = True
    if args.target == 'sim':
        sim(args)
    if args.target == 'simrun':
        simrun(args.erase, extra)
    if args.target in ('sedona', 'all'):
        sedona(args)
    if args.target in ('fw', 'all'):
        idf(args.profile, 'build')
    if args.target == 'qemu':
        extra = args.qemu_args
        if args.profile == 'qemu':
            extra = '-nic user,model=open_eth,hostfwd=udp::1876-:1876 ' + extra
        idf(args.profile, 'qemu', *(['--qemu-extra-args', extra.strip()] if extra.strip() else []), 'monitor')


__doc__ = __doc__.replace('@MODULES@', M.describe())

if __name__ == '__main__':
    main()
