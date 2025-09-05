import os
from collections import deque

class GrubModDependencyResolver:
    def __init__(self, mod_dir):
        self.mod_dir = mod_dir
        self.dep_map = {}

    def _read_deps_from_elf(self, path):
        with open(path, 'rb') as f:
            magic = f.read(4)
            if magic != b'\x7fELF':
                print("Not an ELF file:", path)
                return []
            byte_len = f.read(1)
            if byte_len == b'\x01':
                byte_len = 4
            elif byte_len == b'\x02':
                byte_len = 8
            else:
                print("Unknown ELF class:", path)
                return []
            endian = f.read(1)
            if endian == b'\x01':
                is_little = True
            elif endian == b'\x02':
                is_little = False
            else:
                print("Unknown ELF endianness:", path)
                return []
            f.seek(24 + byte_len * 2)
            shoff = int.from_bytes(f.read(4), 'little' if is_little else 'big')
            f.seek(10, os.SEEK_CUR)
            shentsize = int.from_bytes(f.read(2), 'little' if is_little else 'big')
            shnum = int.from_bytes(f.read(2), 'little' if is_little else 'big')
            shstrndx = int.from_bytes(f.read(2), 'little' if is_little else 'big')

            f.seek(shoff + shentsize * shstrndx + 12 + byte_len)
            shstroff = int.from_bytes(f.read(byte_len), 'little' if is_little else 'big')

            for _ in range(shnum):
                f.seek(shoff)
                sh_name_index = int.from_bytes(f.read(4), 'little' if is_little else 'big')
                f.seek(shstroff + sh_name_index)
                name = f.read(8)
                if name == b'.moddeps':
                    f.seek(shoff + 12 + byte_len)
                    sh_offset = int.from_bytes(f.read(byte_len), 'little' if is_little else 'big')
                    sh_size = int.from_bytes(f.read(4), 'little' if is_little else 'big')
                    f.seek(sh_offset)
                    data = f.read(sh_size)
                    return [d.decode('utf-8') for d in data.split(b'\x00') if d]
                f.seek(shoff + 12 + byte_len)
                sh_offset = int.from_bytes(f.read(byte_len), 'little' if is_little else 'big')
                sh_size = int.from_bytes(f.read(4), 'little' if is_little else 'big')
                shoff += shentsize

        return []

    def resolve_dependencies(self, mods = []):
        all_deps = set(mods)
        queue = deque(mods if isinstance(mods, list) else [mods])
        while queue:
            mod = queue.popleft()
            if mod not in all_deps:
                all_deps.add(mod)
            deps = self._read_deps_from_elf(os.path.join(self.mod_dir, mod + '.mod'))
            for dep in deps:
                if dep not in all_deps:
                    queue.append(dep)
                    all_deps.add(dep)
        return list(all_deps)
    
if __name__ == "__main__":
    import sys
    if len(sys.argv) < 3:
        print("Usage: python grub_dep_detect.py <mod_dir> <mod1> [<mod2> ...]")
        sys.exit(1)

    mod_dir = sys.argv[1]
    mods = sys.argv[2:]

    resolver = GrubModDependencyResolver(mod_dir)
    all_deps = resolver.resolve_dependencies(mods)
    print("All dependencies:", all_deps)