"""The generated Z2M converter must publish an `action`, not just a sensor.

Home Assistant only creates an *event* entity (the thing the Hue remotes get)
for an expose literally named `action`. The per-endpoint `*_press_action`
enums are plain sensors and stay for backwards compatibility, so both
representations have to be present at once.

The interesting part is pure JavaScript, so the converter is really loaded
(against stubbed zigbee-herdsman-converters modules) and its fromZigbee
converters are driven with fake messages. The GitHub runner ships node, so
this runs in CI too; where node is missing those two tests skip and the
structural checks still run.
"""

import json
import re
import shutil
import subprocess
from pathlib import Path

import pytest

CONVERTER = Path("zigbee2mqtt/converters/switch_custom.js")

# Stand-ins for the zigbee-herdsman-converters modules the converter imports.
# Everything that is not the action expose collapses to an inert object; only
# `exposes.presets.action` has to be faithful, because that is what identifies
# our extend and carries the list of event types HA will offer.
STUBS = {
    "zigbee-herdsman-converters/lib/modernExtend.js": """
// Several helpers patch result.fromZigbee[0].convert / toZigbee[0].convertSet,
// so the stub has to be shaped like a real modern extend.
const stub = () => ({
    isModernExtend: true,
    exposes: [],
    fromZigbee: [{convert: () => ({})}],
    toZigbee: [{convertSet: async () => ({})}],
});
module.exports = new Proxy({}, {get: () => stub});
""",
    "zigbee-herdsman-converters/lib/utils.js": """
module.exports = {assertString: () => {}};
""",
    # The real bind() dereferences the endpoint, so an undefined one throws
    # exactly the TypeError this test is about. Keep that behaviour.
    "zigbee-herdsman-converters/lib/reporting.js": """
module.exports = {
    bind: async (endpoint, target, clusters) => {
        for (const cluster of clusters) await endpoint.bind(cluster, target);
    },
};
""",
    "zigbee-herdsman-converters/lib/constants.js": """
module.exports = {repInterval: {MAX: 62000, HOUR: 3600, MINUTE: 60}};
""",
    "zigbee-herdsman-converters/lib/exposes.js": """
module.exports = {
    // withEndpoint is what turns one device-wide action into one per button:
    // it renames the property, and Z2M's HA discovery makes a separate event
    // entity out of each property.
    presets: {
        action: (values) => ({
            name: "action",
            property: "action",
            values,
            withEndpoint(endpoint) {
                this.endpoint = endpoint;
                this.property = `action_${endpoint}`;
                return this;
            },
        }),
    },
    access: {STATE: 1, ALL: 7},
};
""",
    "zigbee-herdsman-converters/lib/ota.js": """
module.exports = {zigbeeOTA: {}};
""",
    "zigbee-herdsman/index.js": """
module.exports = {Zcl: {DataType: {
    ENUM8: 0x30, UINT8: 0x20, UINT16: 0x21, BOOLEAN: 0x10, CHAR_STR: 0x42,
    LONG_CHAR_STR: 0x44,
}}};
""",
}

# (zigbeeModel, cluster, message type, endpoint, payload) -> expected action.
# None means "the converter must stay silent" - a message from an endpoint that
# is not a switch must not invent an event.
CASES = [
    # Single gang with a 2EP long-press companion endpoint (ep1 switch, ep3 long).
    ("BSLR1", "genMultistateInput", "attributeReport", 1, {"presentValue": 0}, "switch_0_release"),
    ("BSLR1", "genMultistateInput", "attributeReport", 1, {"presentValue": 1}, "switch_0_press"),
    ("BSLR1", "genMultistateInput", "attributeReport", 1, {"presentValue": 2}, "switch_0_long_press"),
    # A toggle-mode switch reports 3/4 instead, so the mode picks the event.
    ("BSLR1", "genMultistateInput", "attributeReport", 1, {"presentValue": 3}, "switch_0_position_on"),
    ("BSLR1", "genMultistateInput", "attributeReport", 1, {"presentValue": 4}, "switch_0_position_off"),
    ("BSLR1", "genMultistateInput", "readResponse", 1, {"presentValue": 1}, "switch_0_press"),
    ("BSLR1", "genOnOff", "commandOn", 1, {}, "switch_0_on"),
    ("BSLR1", "genOnOff", "commandOff", 1, {}, "switch_0_off"),
    ("BSLR1", "genOnOff", "commandToggle", 1, {}, "switch_0_toggle"),
    # Dimming: the firmware alternates movemode so each long press dims the
    # other way, and releases with a stop.
    ("BSLR1", "genLevelCtrl", "commandMoveWithOnOff", 1, {"movemode": 0}, "switch_0_brightness_move_up"),
    ("BSLR1", "genLevelCtrl", "commandMoveWithOnOff", 1, {"movemode": 1}, "switch_0_brightness_move_down"),
    ("BSLR1", "genLevelCtrl", "commandStopWithOnOff", 1, {}, "switch_0_brightness_stop"),
    # The 2EP companion endpoint only ever toggles its own bindings.
    ("BSLR1", "genOnOff", "commandToggle", 3, {}, "switch_0_long_toggle"),
    # ... and has no dimming of its own.
    ("BSLR1", "genLevelCtrl", "commandStopWithOnOff", 3, {}, None),
    # The relay endpoint is not a button; its onOff traffic is state, not an event.
    ("BSLR1", "genOnOff", "commandToggle", 2, {}, None),
    ("BSLR1", "genMultistateInput", "attributeReport", 2, {"presentValue": 1}, None),
    # Two gangs: positional prefixes, whatever the endpoint names are.
    ("TS0002-BSMN", "genMultistateInput", "attributeReport", 1, {"presentValue": 1}, "switch_0_press"),
    ("TS0002-BSMN", "genMultistateInput", "attributeReport", 2, {"presentValue": 2}, "switch_1_long_press"),
    ("TS0002-BSMN", "genOnOff", "commandToggle", 5, {}, "switch_0_long_toggle"),
    ("TS0002-BSMN", "genOnOff", "commandToggle", 6, {}, "switch_1_long_toggle"),
    # Cover switch: its own state set, plus the commands it sends to bindings.
    ("TS130F-GIR", "genMultistateInput", "attributeReport", 1, {"presentValue": 1}, "cover_switch_0_open"),
    ("TS130F-GIR", "genMultistateInput", "attributeReport", 1, {"presentValue": 3}, "cover_switch_0_stop"),
    ("TS130F-GIR", "genMultistateInput", "attributeReport", 1, {"presentValue": 4}, "cover_switch_0_long_open"),
    ("TS130F-GIR", "genMultistateInput", "attributeReport", 1, {"presentValue": 5}, "cover_switch_0_long_close"),
    ("TS130F-GIR", "closuresWindowCovering", "commandUpOpen", 1, {}, "cover_switch_0_cover_open"),
    ("TS130F-GIR", "closuresWindowCovering", "commandDownClose", 1, {}, "cover_switch_0_cover_close"),
    ("TS130F-GIR", "closuresWindowCovering", "commandStop", 1, {}, "cover_switch_0_cover_stop"),
]

HARNESS = """
const path = require("path");
const definitions = require(process.argv[2]);

const findByModel = (zbModel) => {
    const d = definitions.find((d) => d.zigbeeModel.includes(zbModel));
    if (!d) throw new Error(`no definition for ${zbModel}`);
    return d;
};
const actionExtend = (d) => {
    const ext = d.extend.find(
        (e) => e.exposes && e.exposes.some((x) => x.name === "action"));
    if (!ext) throw new Error(`no action expose in ${d.model}`);
    return ext;
};

const out = {actions: {}, perButton: {}, results: [], perButtonResults: []};
for (const zbModel of JSON.parse(process.argv[3])) {
    const exposes = actionExtend(findByModel(zbModel)).exposes;
    out.actions[zbModel] = exposes.find((x) => x.property === "action").values;
    // One event entity per button: same expose name, endpoint-suffixed property.
    out.perButton[zbModel] = Object.fromEntries(
        exposes.filter((x) => x.endpoint).map((x) => [x.property, x.values]));
}
// A device joined before `2EP` was enabled has no companion endpoint yet, so
// getEndpoint() returns undefined for it. configure() must skip it rather than
// throw - a throw aborts the whole configure and leaves reporting unset.
const runConfigure = async (zbModel, availableEndpoints, calls) => {
    const endpoint = (ID) => ({
        ID,
        configureReporting: async (cluster, items) => {
            if (calls) calls.reporting.push([ID, cluster, items]);
        },
        read: async (cluster, attrs) => {
            if (calls) calls.read.push([ID, cluster, attrs]);
        },
        bind: async () => {},
    });
    const device = {
        getEndpoint: (ID) => (availableEndpoints.includes(ID) ? endpoint(ID) : undefined),
    };
    try {
        await findByModel(zbModel).configure(device, endpoint(1), console);
        return "ok";
    } catch (e) {
        return `threw: ${e.message}`;
    }
};

for (const c of JSON.parse(process.argv[4])) {
    const [zbModel, cluster, type, endpoint, data] = c;
    const ext = actionExtend(findByModel(zbModel));
    const msg = {endpoint: {ID: endpoint}, type, cluster, data};
    let action = null;
    let perButton = null;
    for (const fz of ext.fromZigbee) {
        if (fz.cluster !== cluster || !fz.type.includes(type)) continue;
        const r = fz.convert(findByModel(zbModel), msg, () => {}, {}, {});
        if (r && r.action !== undefined) action = r.action;
        if (r) {
            for (const [k, v] of Object.entries(r)) {
                if (k.startsWith("action_")) perButton = [k, v];
            }
        }
    }
    out.results.push(action);
    out.perButtonResults.push(perButton);
}

// startUpColorTemperature 0xFFFF ("previous") is refused by zigbee-herdsman
// 10.6.1 before it reaches the device, so the converter has to put a different
// value on the wire and keep showing 65535 in the state.
const runColorTempStartup = (zbModel) => {
    const d = findByModel(zbModel);
    const written = [];
    const entity = {
        write: async (cluster, payload) => written.push([cluster, payload]),
        read: async () => {},
    };
    // z-h-c concatenates the extends in order: the first matching toZigbee
    // wins, while every matching fromZigbee runs and later results overwrite.
    const toZigbee = d.extend.flatMap((e) => e.toZigbee || []);
    const fromZigbee = d.extend.flatMap((e) => e.fromZigbee || []);
    const tz = toZigbee.find((c) => c.key?.includes("color_temp_startup"));
    if (!tz) return {error: "no color_temp_startup converter"};
    const fz = fromZigbee.filter(
        (c) => c.cluster === "lightingColorCtrl" && c.type?.includes("readResponse"));

    const readBack = (endpointID, value) => {
        const msg = {endpoint: {ID: endpointID}, data: {startUpColorTemperature: value},
                     type: "readResponse", cluster: "lightingColorCtrl"};
        let payload = {};
        for (const c of fz) Object.assign(payload, c.convert(d, msg, () => {}, {}, {}) || {});
        return payload;
    };

    return (async () => ({
        previous: {
            state: (await tz.convertSet(entity, "color_temp_startup", 65535, {})).state,
            written: written.splice(0).pop(),
        },
        plain: {
            state: (await tz.convertSet(entity, "color_temp_startup", 250, {})).state,
            written: written.splice(0).pop(),
        },
        readPrevious: readBack(3, 0),
        readPlain: readBack(3, 250),
    }))();
};

(async () => {
    out.colorTempStartup = await runColorTempStartup("GL-C-006P-CCT");
    // The overload alarm is only ever pushed by the device, so it is worth
    // nothing unless configure() asks for it to be reported.
    const meterCalls = {reporting: [], read: []};
    out.meterConfigure = await runConfigure("TS011F-A1Z", [1, 2, 3], meterCalls);
    out.meterCalls = meterCalls;
    // BSLR1 has switch=1, relay=2, switch_long=3.
    out.configure = {
        all_endpoints: await runConfigure("BSLR1", [1, 2, 3]),
        without_2ep_endpoint: await runConfigure("BSLR1", [1, 2]),
    };
    console.log(JSON.stringify(out));
})();
"""


def _node_run(tmp_path):
    """Load the generated converter under node against stubbed z-h-c modules."""
    for rel, source in STUBS.items():
        target = tmp_path / "node_modules" / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(source)
    # zigbee-herdsman is required by bare name, so it needs a package entry.
    (tmp_path / "node_modules" / "zigbee-herdsman" / "package.json").write_text(
        json.dumps({"name": "zigbee-herdsman", "main": "index.js"})
    )
    harness = tmp_path / "harness.js"
    harness.write_text(HARNESS)
    # Node resolves node_modules relative to the requiring file, so the
    # converter has to sit next to the stubs rather than in the repo.
    converter = tmp_path / CONVERTER.name
    converter.write_text(CONVERTER.read_text())
    models = sorted({c[0] for c in CASES})
    proc = subprocess.run(
        [
            "node",
            str(harness),
            str(converter),
            json.dumps(models),
            json.dumps([list(c[:5]) for c in CASES]),
        ],
        cwd=tmp_path,
        capture_output=True,
        text=True,
    )
    assert proc.returncode == 0, proc.stderr
    return json.loads(proc.stdout)


@pytest.fixture(scope="module")
def converter_run(tmp_path_factory):
    if not shutil.which("node"):
        pytest.skip("node is not available; the structural checks still run")
    return _node_run(tmp_path_factory.mktemp("z2m"))


def test_converter_emits_expected_actions(converter_run):
    got = converter_run["results"]
    expected = [c[5] for c in CASES]
    mismatches = {
        f"{c[0]} {c[1]}/{c[2]} ep{c[3]} {c[4]}": (want, have)
        for c, want, have in zip(CASES, expected, got)
        if want != have
    }
    assert not mismatches, mismatches


def test_every_emitted_action_is_declared(converter_run):
    """HA only fires event types the expose advertises."""
    undeclared = {}
    for case, action in zip(CASES, converter_run["results"]):
        if action is None:
            continue
        declared = converter_run["actions"][case[0]]
        if action not in declared:
            undeclared[case[0]] = action
    assert not undeclared, undeclared


def test_each_button_gets_its_own_event_entity(converter_run):
    """One combined `action` leaves HA with a single event entity for the whole
    device, so a two-button device has one entity whose types happen to be
    named switch_0_* and switch_1_*. Endpoint-scoped actions give each button
    an entity of its own, which is what a per-button automation wants."""
    per_button = converter_run["perButton"]["TS0002-BSMN"]
    assert sorted(per_button) == ["action_switch_left", "action_switch_right"]
    for values in per_button.values():
        # Unprefixed: the button is the entity, so repeating it in the type
        # would only be noise.
        assert "press" in values
        assert "long_press" in values
        assert "toggle" in values
        assert "brightness_move_up" in values
        # The 2EP companion endpoint lands on its parent button, marked long_.
        assert "long_toggle" in values
        assert not any(v.startswith("switch_") for v in values)


def test_a_button_event_names_its_own_entity(converter_run):
    """The endpoint the message came from decides which entity is fed."""
    got = {
        f"{c[0]} ep{c[3]} {c[1]}/{c[2]}": pb
        for c, pb in zip(CASES, converter_run["perButtonResults"])
        if c[0] == "TS0002-BSMN"
    }
    assert got == {
        "TS0002-BSMN ep1 genMultistateInput/attributeReport": ["action_switch_left", "press"],
        "TS0002-BSMN ep2 genMultistateInput/attributeReport": ["action_switch_right", "long_press"],
        "TS0002-BSMN ep5 genOnOff/commandToggle": ["action_switch_left", "long_toggle"],
        "TS0002-BSMN ep6 genOnOff/commandToggle": ["action_switch_right", "long_toggle"],
    }


def test_every_per_button_event_is_declared(converter_run):
    """HA only fires event types the entity advertises."""
    undeclared = {}
    for case, emitted in zip(CASES, converter_run["perButtonResults"]):
        if emitted is None:
            continue
        prop, value = emitted
        declared = converter_run["perButton"][case[0]].get(prop, [])
        if value not in declared:
            undeclared[f"{case[0]} {prop}"] = value
    assert not undeclared, undeclared


def test_the_combined_action_is_unchanged(converter_run):
    """Automations already built on the device-wide action must keep working."""
    combined = converter_run["actions"]["TS0002-BSMN"]
    assert "switch_0_press" in combined
    assert "switch_1_long_press" in combined
    assert "switch_1_long_toggle" in combined


def test_previous_colour_temp_startup_avoids_the_rejected_sentinel(converter_run):
    """0xFFFF never reaches the device on zigbee-herdsman 10.6.1.

    Its attribute definition caps startUpColorTemperature at 0xFEFF and the
    write path does not consult the sentinel it defines for exactly this, so
    the write is refused with INVALID_VALUE before it is sent. 0 goes on the
    wire instead - the firmware reads it as "previous" too - while the state
    keeps saying 65535 so the UI's "previous" preset stays selected.
    """
    got = converter_run["colorTempStartup"]
    assert got["previous"]["written"] == [
        "lightingColorCtrl", {"startUpColorTemperature": 0}
    ]
    assert got["previous"]["state"] == {"color_temp_startup": 65535}


def test_an_ordinary_colour_temp_startup_is_passed_through(converter_run):
    got = converter_run["colorTempStartup"]
    assert got["plain"]["written"] == [
        "lightingColorCtrl", {"startUpColorTemperature": 250}
    ]
    assert got["plain"]["state"] == {"color_temp_startup": 250}


def test_the_read_back_shows_previous_again(converter_run):
    got = converter_run["colorTempStartup"]
    assert got["readPrevious"] == {"color_temp_startup_light_0": 65535}
    # Anything else is reported as-is by the stock converter.
    assert got["readPlain"].get("color_temp_startup_light_0") != 65535


def test_the_overload_alarm_is_configured_to_report(converter_run):
    """The alarm only ever arrives unsolicited.

    It is the attribute that says the relay tripped, and nothing polls it, so
    without a reporting configuration the entity simply never changes - which
    is exactly how a real overload passed unnoticed.
    """
    reporting = converter_run["meterCalls"]["reporting"]
    elec = [items for _, cluster, items in reporting
            if cluster == "haElectricalMeasurement"]
    assert elec, "no haElectricalMeasurement reporting configured"
    alarm = [
        i for items in elec for i in items
        if isinstance(i.get("attribute"), dict) and i["attribute"].get("ID") == 0xFF36
    ]
    assert len(alarm) == 1, alarm
    # On change, and re-sent within the hour so a lost report cannot leave a
    # tripped relay showing "none" all day.
    assert alarm[0]["minimumReportInterval"] == 0
    assert alarm[0]["maximumReportInterval"] <= 3600


def test_the_overload_alarm_is_read_once_at_configure(converter_run):
    """So the entity has a value before the first trip, not 'unknown'."""
    reads = converter_run["meterCalls"]["read"]
    assert any(
        cluster == "haElectricalMeasurement" and 0xFF36 in attrs
        for _, cluster, attrs in reads
    ), reads


def test_configure_survives_a_missing_2ep_endpoint(converter_run):
    """Z2M learns endpoints at interview time.

    A device that joined before `2EP` was added to its config string has no
    companion endpoint in Z2M's database, and getEndpoint() returns undefined.
    Throwing there aborts configure() completely, so the device is left without
    even its reporting configured until it is re-interviewed.
    """
    assert converter_run["configure"] == {
        "all_endpoints": "ok",
        "without_2ep_endpoint": "ok",
    }


def test_press_action_sensors_are_kept():
    """The event is additive - the old sensors must not disappear."""
    text = CONVERTER.read_text()
    assert "romasku.pressAction(" in text
    assert "romasku.coverSwitchPressAction(" in text


def test_every_switch_device_has_an_action_event():
    """No switch or cover-switch device may be left without an event entity."""
    blocks = CONVERTER.read_text().split("        zigbeeModel: [")[1:]
    missing = []
    for block in blocks:
        model = re.search(r'model: "([^"]+)"', block).group(1)
        has_buttons = (
            "romasku.pressAction(" in block
            or "romasku.coverSwitchPressAction(" in block
        )
        if has_buttons and "romasku.actionEvent(" not in block:
            missing.append(model)
    assert not missing, missing
