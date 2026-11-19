"""Copy only browser application source into a static hosting directory."""
from argparse import ArgumentParser
from pathlib import Path
from shutil import copyfile

ASSETS = ('index.html', 'styles.css')

def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    output = args.output.resolve()
    if output == source:
        parser.error('Choose a separate output directory.')
    output.mkdir(parents=True, exist_ok=True)
    for name in ASSETS:
        copyfile(source / name, output / name)
    print(f'Copied {len(ASSETS)} application assets to {output}')

if __name__ == '__main__':
    main()
