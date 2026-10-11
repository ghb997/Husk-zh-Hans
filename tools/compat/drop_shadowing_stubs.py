#!/usr/bin/env python3
"""drop_shadowing_stubs.py [java-framework dir] -- remove generated methods that hide a real implementation.

A class in gen-src that extends a hand-written class in src/ gets the platform's full method list from genstubs.py, so its
one-line stubs override the superclass's working methods (AnimatedStateListDrawable's mutate() returning null over
StateListDrawable's, IntentService's onStartCommand over Service's). This drops each generated method whose name and
parameter types (by simple name) match a method with a body in the src/ superclass chain, so the real one is inherited.
Run it after genstubs.py / fillmembers.py.
"""
import os, re, sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '../../src/java-framework')
SRC, GEN = os.path.join(ROOT, 'src'), os.path.join(ROOT, 'gen-src')

def classes(base):
    out = {}
    for root, _, fs in os.walk(base):
        for f in fs:
            if f.endswith('.java'):
                p = os.path.join(root, f)
                out[os.path.relpath(p, base)[:-5].replace(os.sep, '.')] = p
    return out

src, gen = classes(SRC), classes(GEN)

def simple(t):
    t = re.sub(r'<.*>', '', t).strip()
    t = t.replace('...', '[]')
    return t.split('.')[-1]

def sig(params):
    params = params.strip()
    if not params:
        return ()
    out = []
    depth = 0; cur = ''
    for ch in params:
        if ch == '<': depth += 1
        elif ch == '>': depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur); cur = ''
        else:
            cur += ch
    out.append(cur)
    types = []
    for p in out:
        p = re.sub(r'@\w+\s*', '', p).replace('final ', '').strip()
        types.append(simple(p.rsplit(' ', 1)[0]))
    return tuple(types)

METHOD = re.compile(r'^    (?:@\w+\s+)*(?:public|protected)\s+(?!static\b)(?!abstract\b)(?:final\s+)?(?:synchronized\s+)?(?:<[^>]+>\s+)?[\w.<>\[\], ?]+?\s+(\w+)\(([^)]*)\)\s*(?:throws [\w., ]+)?\s*\{', re.M)

ABSTRACT = re.compile(r'^    (?:@\w+\s+)*(?:public|protected)\s+abstract\s+[\w.<>\[\], ?]+?\s+(\w+)\(([^)]*)\)', re.M)

def implemented(path):
    s = open(path).read()
    return {(m.group(1), sig(m.group(2))) for m in METHOD.finditer(s)}, {(m.group(1), sig(m.group(2))) for m in ABSTRACT.finditer(s)}, s

def superclass(s):
    m = re.search(r'\bclass\s+\w+(?:<[^>]*>)?\s+extends\s+([\w.]+)', s)
    return m.group(1) if m else None

def resolve(name, pkg):
    if name in src or name in gen:
        return name
    cand = pkg + '.' + name
    if cand in src or cand in gen:
        return cand
    return None

def chain_methods(cls):
    """methods with bodies in the src/ part of cls's superclass chain"""
    found = set(); blocked = set()                    # the nearest declaration wins: an abstract redeclaration hides one further up
    seen = set()
    while cls and cls not in seen:
        seen.add(cls)
        if cls in src:
            ms, abstract, s = implemented(src[cls])
            found |= (ms - blocked)
            blocked |= abstract | ms
        elif cls in gen:
            s = open(gen[cls]).read()
        else:
            break
        sup = superclass(s)
        if not sup:
            break
        pkg = cls.rsplit('.', 1)[0]
        cls = resolve(sup, pkg)
    return found

total = 0
for name, path in sorted(gen.items()):
    s = open(path).read()
    sup = superclass(s)
    if not sup:
        continue
    sup = resolve(sup, name.rsplit('.', 1)[0])
    if not sup:
        continue
    real = chain_methods(sup)
    if not real:
        continue
    lines = s.split('\n'); keep = []; dropped = 0
    for line in lines:
        m = METHOD.match(line)
        # only one-line generated members at class level (4-space indent, body closes on the same line)
        if m and line.rstrip().endswith('}') and line.count('{') == line.count('}') and (m.group(1), sig(m.group(2))) in real:
            dropped += 1
            continue
        keep.append(line)
    if dropped:
        open(path, 'w').write('\n'.join(keep))
        total += dropped
        print(f'{dropped:3d} {name}')
print(f'dropped {total} generated methods that hid real ones')
