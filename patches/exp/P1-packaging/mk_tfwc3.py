# mk_tfwc3.py <in.tfwc2> <out.tfwc3> <patched-tree>: re-encode FX2TFWC2 as FX2TFWC3 (int4 tree: 16-bit probs, shift 9)
# with the patched pysrc/weights_compress.py of <patched-tree> (0002 patch); verifies the v3 file decodes to the
# same tensors and that the v2 writer still reproduces <in> byte for byte. Needs numpy (not torch: the
# torch-importing pysrc.export_weights is stubbed with the four dtype codes weights_compress uses).
import sys, types, numpy as np, os
ew = types.ModuleType('pysrc.export_weights')
ew.DTYPE_INT8, ew.DTYPE_BF16_BITS, ew.DTYPE_FLOAT32, ew.DTYPE_INT32 = 0, 1, 2, 3
ew._DTYPE_TO_NUMPY = {0: np.int8, 1: np.uint16, 2: np.float32, 3: np.int32}
ew.read_tensor_file = None
sys.path.insert(0, sys.argv[3])
import pysrc
sys.modules['pysrc.export_weights'] = ew
from pysrc.weights_compress import read_tensor_file_v2, write_tensor_file_v2, _verify_equal
src = read_tensor_file_v2(sys.argv[1])
write_tensor_file_v2(sys.argv[2], [(n, c, a) for n, (c, a) in src.items()], v3=True)
_verify_equal(src, read_tensor_file_v2(sys.argv[2]))
# the v2 writer must still reproduce the original file byte for byte
tmp = sys.argv[2] + '.v2check'
write_tensor_file_v2(tmp, [(n, c, a) for n, (c, a) in src.items()])
assert open(tmp, 'rb').read() == open(sys.argv[1], 'rb').read(), 'v2 writer does not reproduce the input'
os.remove(tmp)
print(f'{sys.argv[1]} ({os.path.getsize(sys.argv[1])}) -> {sys.argv[2]} ({os.path.getsize(sys.argv[2])}), verified')
