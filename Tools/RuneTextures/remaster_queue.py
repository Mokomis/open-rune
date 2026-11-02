"""Inventory, deduplicate and track a resumable generative remaster job set.

This tool does not call an image service. Generation uses the selected image tool;
every source is inspected and each resulting master is reviewed before packing.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import shutil


def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def write_json(path, value):
    temp = path.with_suffix(path.suffix + '.tmp')
    temp.write_text(json.dumps(value, indent=2) + '\n')
    temp.replace(path)


def policy(row):
    if row['status'] != 'exported':
        return 'original:procedural' if row['status'] == 'procedural-original' else 'blocked:source-format'
    base = re.sub(r'(101|107)$', '', row['id'].split('.')[0].lower())
    leaf = row['id'].split('.')[-1].lower()
    if base in {'engine', 'editor', 'uwindow', 'uwindowfonts', 'ubrowser', 'rmenu', 'runemenus', 'runeserveradmin', 'palettes'} or leaf in {'screenshot', 'levelshot'}:
        return 'original:interface-or-map-preview'
    if row['animated']:
        return 'deferred:coherent-animation-sequence'
    if base.startswith(('runefx', 'bloodfx', 'fogout')) or base == 'fluid' or row['flags'] & (4 | 64):
        return 'deferred:effect-or-blended-surface'
    if base == 'sky':
        return 'deferred:coherent-sky-set'
    if min(row['width'], row['height']) < 32:
        return 'original:utility-texture'
    if max(row['width'], row['height']) > 1024 or any(n & (n-1) for n in (row['width'], row['height'])):
        return 'blocked:runtime-dimensions'
    return 'generate'


def category(row):
    base = re.sub(r'(101|107)$', '', row['id'].split('.')[0].lower())
    if base.startswith(('players', 'creatures', 'vwcreatures')):
        return 'character-atlas'
    if base.startswith(('weapons', 'objects', 'plants', 'decap', 'junmodel')):
        return 'object-atlas'
    return 'environment'


def prompt(row):
    kind = category(row)
    return (
        'Use case: precise-object-edit. Production texture remaster for Open Rune. '
        'The supplied original is the EDIT TARGET. Return only the flat texture edge-to-edge. '
        f'Asset identity: {row["id"]}. Preserve its exact {row["width"]}:{row["height"]} aspect ratio. '
        'Keep every large shape, boundary, normalized landmark position, silhouette, symbol, '
        'UV island and blank region exactly aligned with the original. Do not turn an atlas '
        'into a portrait, assembled object or scene. Preserve the original regional hue, '
        'saturation and brightness: dark brown stays dark brown, grey stays grey. '
        'Add substantial fine material detail appropriate to what is already present: '
        'grain, pores, wear, metal pitting, small fibers or hair strands. Do not invent '
        'new design elements, outlines, cracks, scars, fasteners, markings or text. '
        'Retain all original lighting cues but add no baked glare, specular highlights, '
        'directional lighting, cast shadows or bloom. No border, watermark or comparison. '
        + ('This is a character UV atlas: preserve facial identity, eye/mouth positions, '
           'body proportions and existing anatomy. Keep intentionally stretched shapes. '
           if kind == 'character-atlas' else
           'This is an object UV atlas: retain all pieces and their exact layout and orientation. '
           if kind == 'object-atlas' else
           'This is environment artwork: retain every course, plank, ornament and feature. '
           'Preserve edge continuity without mirroring or inserting repeated central motifs. ')
        + 'Prioritize exact registration and original colors over inventive detail.'
    )


def create(args):
    inv = json.loads((args.source / 'inventory.json').read_text())
    if inv['failures'] or not inv['original_packages_unchanged']:
        raise ValueError('Resolve source inventory failures first')
    args.output.mkdir(parents=True, exist_ok=False)
    for name in ('jobs', 'masters', 'receipts', 'review', 'rejected'):
        (args.output / name).mkdir()
    groups, coverage = {}, []
    identities = set()
    for row in inv['textures']:
        identity = row['id'].lower()
        if identity in identities:
            raise ValueError('Case-insensitive texture identity collision: ' + identity)
        identities.add(identity)
        status = policy(row)
        item = {'id': row['id'], 'status': status}
        if status == 'generate':
            source = args.source / row['directory'] / row['file']
            if sha(source) != row['sha256']:
                raise ValueError('Source changed: ' + row['id'])
            # One creative master per distinct RGB source; each alias retains its
            # own original mask, flags, material recipe and runtime identity.
            key = row['sha256']
            groups.setdefault(key, []).append(row)
            item['job'] = key
        coverage.append(item)
    jobs = []
    for key, aliases in groups.items():
        aliases.sort(key=lambda r: (bool(re.search(r'(101|107)\.', r['id'])), r['id'].lower()))
        row = aliases[0]
        job = {'schema': 1, 'id': key, 'status': 'pending', 'primary': row['id'],
               'source': str((args.source / row['directory'] / row['file']).resolve()),
               'source_sha256': key, 'size': [row['width'], row['height']],
               'category': category(row), 'prompt': prompt(row), 'aliases': aliases,
               'review': {'input_inspected': False, 'layout_approved': False, 'materials_approved': False}}
        write_json(args.output / 'jobs' / (key + '.json'), job)
        jobs.append({'id': key, 'primary': row['id'], 'category': job['category']})
    jobs.sort(key=lambda j: ({'character-atlas': 0, 'object-atlas': 1, 'environment': 2}[j['category']], j['primary'].lower()))
    summary = {'schema': 1, 'inventory_sha256': sha(args.source / 'inventory.json'),
               'source_root': str(args.source.resolve()), 'logical_textures': len(coverage),
               'coverage': dict(collections.Counter(r['status'] for r in coverage)),
               'unique_generation_jobs': len(jobs), 'jobs': jobs,
               'rule': 'No generated master is called complete until reviewed and packaged. Deferred groups remain explicit.',
               'generation_policy': 'Built-in ChatGPT image generation under included Pro usage only. No API key, purchased credits, or paid fallback.'}
    write_json(args.output / 'plan.json', summary)
    write_json(args.output / 'coverage.json', coverage)
    print(json.dumps({k: v for k, v in summary.items() if k != 'jobs'}, indent=2))


def ingest(args):
    path = args.queue / 'jobs' / (args.job + '.json')
    job = json.loads(path.read_text())
    if job['status'] != 'pending':
        raise ValueError('Job already has a master; use a new revision instead of overwriting it')
    if sha(Path(job['source'])) != job['source_sha256']:
        raise ValueError('Source changed')
    from PIL import Image
    with Image.open(args.image) as im:
        w, h = im.size
        ratio_error = abs((w/h) / (job['size'][0]/job['size'][1]) - 1)
        if ratio_error > .02 or min(w, h) < min(job['size']):
            raise ValueError('Generated aspect ratio or resolution mismatch')
        im.verify()
    dest = args.queue / 'masters' / (args.job + '.png')
    # Canonical PNG output, without modifying the generator's original result.
    with Image.open(args.image) as im:
        candidate = dest.with_suffix('.pending.png')
        im.convert('RGB').save(candidate)
    if dest.exists() and sha(dest) != sha(candidate):
        candidate.unlink()
        raise ValueError('A different master already exists for this job')
    candidate.replace(dest)
    job.update(status='review', master=str(dest.relative_to(args.queue)), master_sha256=sha(dest),
               generated_original=str(args.image.resolve()), generator=args.generator)
    if args.prompt_file:
        job['generation_prompt'] = args.prompt_file.read_text().strip()
    job['review']['input_inspected'] = args.inspected
    write_json(path, job)
    print(job['primary'], 'awaiting layout/material review')


def revise(args):
    """Archive a rejected review candidate before replacing it with a new image."""
    path = args.queue / 'jobs' / (args.job + '.json')
    job = json.loads(path.read_text())
    if job['status'] not in ('review', 'rejected'):
        raise ValueError('Only review/rejected candidates can be revised')
    if sha(Path(job['source'])) != job['source_sha256']:
        raise ValueError('Source changed')
    old_master = args.queue / job['master']
    if sha(old_master) != job['master_sha256']:
        raise ValueError('Existing master changed')
    from PIL import Image
    with Image.open(args.image) as im:
        w, h = im.size
        ratio_error = abs((w/h) / (job['size'][0]/job['size'][1]) - 1)
        if ratio_error > .02 or min(w, h) < min(job['size']):
            raise ValueError('Generated aspect ratio or resolution mismatch')
        im.verify()
    archive = args.queue / 'revisions' / args.job / job['master_sha256']
    if archive.exists():
        raise ValueError('This revision has already been archived')
    archive.mkdir(parents=True)
    shutil.copy2(path, archive / 'job.json')
    shutil.copy2(old_master, archive / 'master.png')
    old_audit = args.queue / 'review' / (args.job + '.json')
    if old_audit.exists():
        shutil.copy2(old_audit, archive / 'audit.json')
        old_audit.unlink()
    job['status'] = 'pending'
    for key in ('master', 'master_sha256', 'generated_original', 'generator', 'generation_prompt'):
        job.pop(key, None)
    job['review'] = {'input_inspected': False, 'layout_approved': False, 'materials_approved': False}
    old_master.unlink()
    write_json(path, job)
    ingest(args)


def record_failed(args):
    """Keep an unusable generated candidate without changing a pending job."""
    path = args.queue / 'jobs' / (args.job + '.json')
    job = json.loads(path.read_text())
    if job['status'] != 'pending':
        raise ValueError('Only pending jobs can record a failed candidate')
    if sha(Path(job['source'])) != job['source_sha256']:
        raise ValueError('Source changed')
    digest = sha(args.image)
    folder = args.queue / 'rejected' / args.job
    folder.mkdir(parents=True, exist_ok=True)
    dest = folder / (digest + args.image.suffix.lower())
    if not dest.exists():
        shutil.copy2(args.image, dest)
    note = {'job': args.job, 'primary': job['primary'], 'source_sha256': job['source_sha256'],
            'candidate_sha256': digest, 'candidate': str(dest.relative_to(args.queue)),
            'generator': args.generator, 'reason': args.reason,
            'prompt': args.prompt_file.read_text().strip() if args.prompt_file else job['prompt']}
    write_json(folder / (digest + '.json'), note)
    print(job['primary'], 'candidate retained; job remains pending')


def audit(args):
    """Measure broad drift; approval still requires looking at the artwork."""
    import numpy as np
    from PIL import Image, ImageFilter
    path = args.queue / 'jobs' / (args.job + '.json')
    job = json.loads(path.read_text())
    if job['status'] not in ('review', 'approved'):
        raise ValueError('Only generated images can be audited')
    master = args.queue / job['master']
    if sha(master) != job['master_sha256'] or sha(Path(job['source'])) != job['source_sha256']:
        raise ValueError('Source or master changed since ingest')
    with Image.open(job['source']) as im:
        source = im.convert('RGB')
    with Image.open(master) as im:
        master_size = list(im.size)
        generated = im.convert('RGB').resize(source.size, Image.Resampling.LANCZOS)
    # Blur both at source resolution: fine detail is intentionally new. Large
    # differences and lost boundaries need review before UV-atlas use.
    radius = max(source.size) / 48
    a = np.asarray(source.filter(ImageFilter.GaussianBlur(radius)), dtype=np.float32) / 255
    b = np.asarray(generated.filter(ImageFilter.GaussianBlur(radius)), dtype=np.float32) / 255
    delta = np.abs(a - b)
    def edges(v):
        grey = v @ np.array([.2126, .7152, .0722], dtype=np.float32)
        gy, gx = np.gradient(grey)
        return np.hypot(gx, gy)
    ea, eb = edges(a), edges(b)
    # Edge agreement is normalized so darker artwork is still comparable.
    denominator = float(np.maximum(ea, eb).sum())
    agreement = float(np.minimum(ea, eb).sum() / denominator) if denominator > 1e-8 else 1.0
    report = {'source_sha256': job['source_sha256'], 'master_sha256': job['master_sha256'],
              'source_size': list(source.size), 'master_size': master_size,
              'mean_rgb_source': [round(float(v), 4) for v in a.mean((0, 1))],
              'mean_rgb_generated': [round(float(v), 4) for v in b.mean((0, 1))],
              'broad_mean_abs_difference': round(float(delta.mean()), 4),
              'broad_p95_abs_difference': round(float(np.percentile(delta, 95)), 4),
              'boundary_agreement': round(agreement, 4),
              'judgment': 'Measurements flag drift; visual UV/material review is still required.'}
    write_json(args.queue / 'review' / (args.job + '.json'), report)
    print(json.dumps(report, indent=2))


def decide(args):
    path = args.queue / 'jobs' / (args.job + '.json')
    job = json.loads(path.read_text())
    if job['status'] != 'review':
        raise ValueError('Only assets awaiting review can be decided')
    if not job['review']['input_inspected']:
        raise ValueError('Inspect the original before deciding')
    report = args.queue / 'review' / (args.job + '.json')
    if not report.exists():
        raise ValueError('Run audit first')
    proof = json.loads(report.read_text())
    if proof['master_sha256'] != job['master_sha256']:
        raise ValueError('Audit belongs to an older master')
    job['review'].update(layout_approved=args.decision == 'approve',
                         materials_approved=args.decision == 'approve',
                         note=args.note)
    job['status'] = 'approved' if args.decision == 'approve' else 'rejected'
    write_json(path, job)
    print(job['primary'], job['status'])


def export_recipe(args):
    """Write an immutable builder input from approved masters and every alias."""
    if args.output.exists():
        raise ValueError('Refusing to replace an existing batch recipe')
    plan = json.loads((args.queue / 'plan.json').read_text())
    source_root = Path(plan['source_root']) / 'source'
    materials = json.loads(args.materials.read_text()) if args.materials else {}
    assets, neutral = [], []
    for descriptor in plan['jobs']:
        job = json.loads((args.queue / 'jobs' / (descriptor['id'] + '.json')).read_text())
        if job['status'] != 'approved':
            continue
        master = args.queue / job['master']
        if sha(master) != job['master_sha256']:
            raise ValueError('Approved master changed: ' + job['primary'])
        primary_material = materials.get(job['primary'].lower())
        for alias in job['aliases']:
            identity = alias['id']
            source = Path(plan['source_root']) / alias['directory'] / alias['file']
            if sha(source) != alias['sha256']:
                raise ValueError('Source changed: ' + identity)
            if identity.lower() in materials:
                material = materials[identity.lower()].copy()
            elif primary_material:
                material = primary_material.copy()
            else:
                material = {'material': 'neutral'}
                neutral.append(identity)
            if material['material'] == 'stone' or any(r['material'] == 'stone' for r in material.get('regions', [])):
                material.setdefault('stone_detail_gain', 0.7)
            assets.append({'identity': identity,
                           'source': str(source.relative_to(source_root)),
                           'master': master.name,
                           'source_sha256': alias['sha256'],
                           'master_sha256': job['master_sha256'],
                           **material})
    assets.sort(key=lambda a: a['identity'].lower())
    recipe = {'version': 1, 'scope': 'approved queue masters only',
              'assets': assets, 'neutral_material_aliases': neutral,
              'inventory_sha256': plan['inventory_sha256']}
    write_json(args.output, recipe)
    print(json.dumps({'approved_assets': len(assets), 'neutral_material_aliases': len(neutral),
                      'output': str(args.output)}, indent=2))


def status(args):
    counts = collections.Counter(json.loads(p.read_text())['status'] for p in (args.queue / 'jobs').glob('*.json'))
    print(json.dumps(counts, indent=2))


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('plan'); p.add_argument('--source', type=Path, required=True); p.add_argument('--output', type=Path, required=True); p.set_defaults(run=create)
    p = sub.add_parser('ingest'); p.add_argument('--queue', type=Path, required=True); p.add_argument('--job', required=True); p.add_argument('--image', type=Path, required=True); p.add_argument('--generator', required=True); p.add_argument('--prompt-file', type=Path); p.add_argument('--inspected', action='store_true'); p.set_defaults(run=ingest)
    p = sub.add_parser('revise'); p.add_argument('--queue', type=Path, required=True); p.add_argument('--job', required=True); p.add_argument('--image', type=Path, required=True); p.add_argument('--generator', required=True); p.add_argument('--prompt-file', type=Path); p.add_argument('--inspected', action='store_true'); p.set_defaults(run=revise)
    p = sub.add_parser('record-failed'); p.add_argument('--queue', type=Path, required=True); p.add_argument('--job', required=True); p.add_argument('--image', type=Path, required=True); p.add_argument('--generator', required=True); p.add_argument('--prompt-file', type=Path); p.add_argument('--reason', required=True); p.set_defaults(run=record_failed)
    p = sub.add_parser('audit'); p.add_argument('--queue', type=Path, required=True); p.add_argument('--job', required=True); p.set_defaults(run=audit)
    p = sub.add_parser('decide'); p.add_argument('--queue', type=Path, required=True); p.add_argument('--job', required=True); p.add_argument('--decision', choices=('approve', 'reject'), required=True); p.add_argument('--note', required=True); p.set_defaults(run=decide)
    p = sub.add_parser('export-recipe'); p.add_argument('--queue', type=Path, required=True); p.add_argument('--materials', type=Path); p.add_argument('--output', type=Path, required=True); p.set_defaults(run=export_recipe)
    p = sub.add_parser('status'); p.add_argument('--queue', type=Path, required=True); p.set_defaults(run=status)
    args = ap.parse_args(); args.run(args)


if __name__ == '__main__':
    main()
