import numpy as np
import pytest

from nps.fields import ConicalField, Ramp, WaveField
from nps.transform import Deformation


@pytest.fixture(params=["wave", "conical", "wave_flat_top"])
def deform(request):
    if request.param == "wave":
        return Deformation(WaveField(amplitude=0.8, wavelength=16), Ramp())
    if request.param == "conical":
        return Deformation(ConicalField(angle_deg=10), Ramp(z_ramp=15))
    return Deformation(WaveField(), Ramp(flat_top=True, z_top=40))


def test_roundtrip(deform):
    rng = np.random.default_rng(0)
    pts = rng.uniform([-20, -20, 0], [20, 20, 40], size=(2000, 3))
    back = deform.to_real(deform.to_slice(pts))
    assert np.allclose(back, pts, atol=1e-7)


def test_first_layers_flat(deform):
    x = np.linspace(-20, 20, 50)
    for z in (0.0, 0.2, 0.35, 0.6):
        assert np.allclose(deform.to_real_z(x, x, np.full_like(x, z)), z)


def test_jacobian_matches_numeric(deform):
    x, y, z = 3.1, -7.4, np.linspace(0.5, 30, 40)
    h = 1e-5
    zs = deform.to_slice(np.c_[np.full_like(z, x), np.full_like(z, y), z])[:, 2]
    zs2 = deform.to_slice(np.c_[np.full_like(z, x), np.full_like(z, y), z + h])[:, 2]
    assert np.allclose(deform.jacobian(x, y, z), h / (zs2 - zs), rtol=1e-4)


def test_validate_flags_short_ramp():
    d = Deformation(WaveField(amplitude=1.0), Ramp(z_ramp=1.0))
    verts = np.array([[0, 0, 0], [20, 20, 20.0]])
    assert any("rampa" in p for p in d.validate(verts, max_slope_deg=45))


def test_validate_flags_steep_slope():
    d = Deformation(WaveField(amplitude=2.0, wavelength=8), Ramp(z_ramp=20))
    verts = np.array([[0, 0, 0], [20, 20, 40.0]])
    assert any("Pendiente" in p for p in d.validate(verts, max_slope_deg=20))
