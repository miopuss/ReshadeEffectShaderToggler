"""Package CI and releases from one checked-in content manifest; no dependencies."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import zipfile


def entries(root, mapping):
    for destination, source in mapping.items():
        source = root / source
        target = PurePosixPath(destination)
        if target.is_absolute() or '..' in target.parts or not source.resolve().is_relative_to(root.resolve()):
            raise ValueError('Package paths must remain inside the repository/archive')
        if not source.exists():
            raise FileNotFoundError(source)
        if source.is_dir():
            for file in sorted(source.rglob('*')):
                if file.is_file():
                    yield file, str(target / file.relative_to(source).as_posix())
        else:
            yield source, str(target)


def package(root, kind, commit, include_chinese=False):
    if kind not in ('test', 'release') or not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('Expected a package kind and full Git commit identity')
    manifest = json.loads((root / 'scripts/package-manifest.json').read_text(encoding='utf-8'))
    version = re.search(r'^#define REST_VERSION_STRING "([0-9]+(?:\.[0-9]+){3})"',
                        (root / 'src/version.h').read_text(encoding='utf-8-sig'), re.MULTILINE)
    if version is None:
        raise ValueError('Missing four-part source version')
    version = version[1]
    # Validate every input before creating any output archives.
    contents = {key: list(entries(root, manifest[key])) for key in ('main', 'bg3')}
    if include_chinese:
        chinese = {
            target: source.replace('/Release/', '/Release-zh/') if target.endswith(('.addon32', '.addon64')) else source
            for target, source in manifest['main'].items()
        }
        contents['zh'] = list(entries(root, chinese))
    output = root / 'test-package' if kind == 'test' else root
    output.mkdir(parents=True, exist_ok=True)
    main_name = f'REST-test-{version}-{commit[:12]}.zip' if kind == 'test' else 'release.zip'
    bg3_name = 'BG3-Configs-D3D11+Vulkan.zip'
    info = f'REST Enhanced {version}\nCommit: {commit}\nPackage: {kind}\n'
    info += ('Test candidate for runtime validation; not a published stable release.\n' if kind == 'test' else
             'Release package. See docs/releases for changes and recorded test coverage.\n')
    info += 'Compilation and CPU tests do not establish game/GPU runtime correctness.\n'
    main_archives = {main_name: 'main'}
    if include_chinese:
        main_archives['release.zh-CN.zip'] = 'zh'
    for name, content in main_archives.items():
        with zipfile.ZipFile(output / name, 'w', zipfile.ZIP_DEFLATED) as archive:
            for source, target in contents[content]:
                archive.write(source, target)
            archive.writestr('BUILD-INFO.txt', info + ('UI language: Simplified Chinese\n' if content == 'zh' else 'UI language: English\n'))
    with zipfile.ZipFile(output / bg3_name, 'w', zipfile.ZIP_DEFLATED) as archive:
        for source, target in contents['bg3']:
            archive.write(source, target)
    hashes = {name: hashlib.sha256((output / name).read_bytes()).hexdigest()
              for name in (*main_archives, bg3_name)}
    (output / 'SHA256SUMS.txt').write_text(''.join(f'{sha}  {name}\n' for name, sha in hashes.items()), encoding='ascii')
    (output / 'BG3-Configs-SHA256SUMS.txt').write_text(f'{hashes[bg3_name]}  {bg3_name}\n', encoding='ascii')
    provenance = (root / 'configs/BG3/PROVENANCE.json').read_text(encoding='utf-8')
    (output / 'BG3-Configs-PROVENANCE.txt').write_text(
        f'BG3 configuration sources: configs/BG3 at commit {commit}\n'
        f'Archive SHA-256: {hashes[bg3_name]}\nOriginal source provenance:\n{provenance}', encoding='utf-8')
    print('Packaged ' + ', '.join(hashes) + '; SHA256SUMS.txt covers all archives.')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kind', choices=('test', 'release'), required=True)
    parser.add_argument('--include-chinese', action='store_true', help='also package the Simplified Chinese release')
    args = parser.parse_args()
    if args.include_chinese and args.kind != 'release':
        parser.error('--include-chinese requires --kind release')
    root = Path(__file__).resolve().parents[1]
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    package(root, args.kind, commit, args.include_chinese)
