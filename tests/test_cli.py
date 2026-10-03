from nps import cli


def test_cr5_preset_fills_defaults(monkeypatch, tmp_path):
    seen = {}
    monkeypatch.setattr(cli, "run", lambda job: seen.setdefault("job", job))
    cli.main(["slice", "x.stl", "-o", str(tmp_path / "x.gcode"), "--printer", "cr5proh",
              "--amplitude", "0.3", "-p", "mio.ini"])
    job = seen["job"]
    assert job.center == (150.0, 112.5)
    assert job.field.amplitude == 0.3            # lo explícito gana al preset
    assert job.field.wavelength == 20.0
    assert job.gcode.z_max_speed == 10.0 and job.gcode.max_feed == 6000.0
    assert job.profiles[0].name == "cr5proh.ini" and job.profiles[1].name == "mio.ini"
