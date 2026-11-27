"""Ed25519 signature verification (RFC 8032, section 6 reference algorithm).

Pure Python and verification-only: the host hub checks one catalog signature per refresh,
so speed is irrelevant and the standard library suffices.
"""

from hashlib import sha512

_P = 2**255 - 19
_Q = 2**252 + 27742317777372353535851937790883648493
_D = -121665 * pow(121666, _P - 2, _P) % _P
_SQRT_M1 = pow(2, (_P - 1) // 4, _P)


def _inverse(x):
    return pow(x, _P - 2, _P)


def _add(p, q):
    a = (p[1] - p[0]) * (q[1] - q[0]) % _P
    b = (p[1] + p[0]) * (q[1] + q[0]) % _P
    c = 2 * p[3] * q[3] * _D % _P
    d = 2 * p[2] * q[2] % _P
    e, f, g, h = b - a, d - c, d + c, b + a
    return e * f % _P, g * h % _P, f * g % _P, e * h % _P


def _multiply(scalar, point):
    result = (0, 1, 1, 0)
    while scalar > 0:
        if scalar & 1:
            result = _add(result, point)
        point = _add(point, point)
        scalar >>= 1
    return result


def _equal(p, q):
    return (p[0] * q[2] - q[0] * p[2]) % _P == 0 and (p[1] * q[2] - q[1] * p[2]) % _P == 0


def _recover_x(y, sign):
    if y >= _P:
        return None
    x2 = (y * y - 1) * _inverse(_D * y * y + 1)
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (_P + 3) // 8, _P)
    if (x * x - x2) % _P:
        x = x * _SQRT_M1 % _P
    if (x * x - x2) % _P:
        return None
    if (x & 1) != sign:
        x = _P - x
    return x


def _decompress(data):
    if len(data) != 32:
        return None
    y = int.from_bytes(data, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    if x is None:
        return None
    return x, y, 1, x * y % _P


_GY = 4 * _inverse(5) % _P
_GX = _recover_x(_GY, 0)
_BASE = (_GX, _GY, 1, _GX * _GY % _P)


def verify(public_key: bytes, message: bytes, signature: bytes) -> bool:
    """True when `signature` is a valid Ed25519 signature of `message` by `public_key`."""
    if len(public_key) != 32 or len(signature) != 64:
        return False
    a = _decompress(public_key)
    r = _decompress(signature[:32])
    if a is None or r is None:
        return False
    s = int.from_bytes(signature[32:], "little")
    if s >= _Q:
        return False
    h = int.from_bytes(sha512(signature[:32] + public_key + message).digest(), "little") % _Q
    return _equal(_multiply(s, _BASE), _add(r, _multiply(h, a)))
