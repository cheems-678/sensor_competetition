"""Build-time collection of local HTML/CSS assets; no runtime state or I/O writes."""
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit


def frontend_asset_paths(directory):
    root = Path(directory).resolve()
    pending = [root / 'index.html']
    found = set()
    while pending:
        asset = pending.pop().resolve()
        if not asset.is_relative_to(root) or not asset.is_file():
            raise RuntimeError(f'Invalid frontend asset reference: {asset}')
        if asset in found:
            continue
        found.add(asset)
        if asset.suffix == '.html':
            references = re.findall(r'(?:src|href)=["\']([^"\']+)', asset.read_text(encoding='utf-8'))
        elif asset.suffix == '.css':
            references = re.findall(r'url\(\s*["\']?([^"\')\s]+)', asset.read_text(encoding='utf-8'))
        else:
            continue
        for reference in references:
            url = urlsplit(reference)
            if url.scheme == 'data' or reference.startswith('#'):
                continue
            if url.scheme or url.netloc:
                raise RuntimeError(f'Remote frontend asset reference: {reference}')
            path = unquote(url.path)
            pending.append(root / path.lstrip('/') if path.startswith('/') else asset.parent / path)
    return sorted(found)
