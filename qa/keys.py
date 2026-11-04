import re, json, sys
def parse(path):
    keys = {}; group = None
    for line in open(path, encoding='utf-8'):
        if line.startswith('## Grupos previstos'): break
        m = re.match(r'^### `(\w+)`$', line.strip())
        if m: group = m.group(1); continue
        if line.startswith('### '): group = None; continue
        if group and line.startswith('| `'):
            cells = [c.strip() for c in line.strip().strip('|').split('|')]
            if len(cells) < 4: continue
            names = re.findall(r'`(\w+)`', cells[0])
            typ, dflt, rng = cells[1], cells[2], cells[3]
            for n in names:
                keys[f'{group}.{n}'] = dict(type=typ, default=dflt, range=rng)
    return keys
if __name__ == '__main__':
    k = parse(sys.argv[1]); json.dump(k, open(sys.argv[2], 'w'), ensure_ascii=False, indent=1)
    import collections; print(len(k), collections.Counter(v['type'] for v in k.values()).most_common(8))
