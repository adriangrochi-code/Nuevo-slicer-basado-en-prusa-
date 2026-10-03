import pytest

from nps import cli, config


def test_presets_load_and_generate_prusa(tmp_path):
    for name in config.list_printers():
        cfg = config.load_config(name)
        ini = config.write_prusa_ini(cfg, tmp_path / f"{name}.ini").read_text()
        assert "bed_shape = " in ini and "machine_limits_usage = time_estimate_only" in ini


def test_priority_and_shortcuts(monkeypatch, tmp_path):
    user = tmp_path / "mio.toml"
    user.write_text('[nonplanar]\namplitude = 0.3\nwavelength = 30.0\n[prusa]\nperimeters = 4\n')
    seen = {}
    monkeypatch.setattr(cli, "run", lambda job: seen.setdefault("job", job))
    cli.main(["slice", "x.stl", "-o", str(tmp_path / "x.gcode"), "--printer", "cr5proh",
              "-c", str(user), "--wavelength", "25", "--set", "print.infill=40"])
    job = seen["job"]
    assert job.center == (150.0, 112.5)              # centro de la cama del preset
    assert job.field.amplitude == 0.3                # el .toml pisa al preset
    assert job.field.wavelength == 25.0              # la línea de comandos pisa al .toml
    assert job.gcode.z_max_speed == 5.0 and job.gcode.max_feed == 6000.0
    prusa = config.prusa_settings(job.config)
    assert prusa["fill_density"] == "40%" and prusa["perimeters"] == 4
    assert prusa["retract_length"] == 5.0


def test_unknown_key_rejected():
    with pytest.raises(ValueError, match="nonplanar.amplitud"):
        config.load_config(sets=["nonplanar.amplitud=1"])


def test_uniform_flow_auto_respects_hotend():
    cfg = config.load_config(sets=["print.speed=500", "printer.max_flow=10"])
    assert config.uniform_flow(cfg) == 8.0
