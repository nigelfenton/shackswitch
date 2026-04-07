#!/usr/bin/env python3
"""
migrate_config.py — ShackSwitch config migration
Converts shackswitch_config.json (flat) to new profile-based structure.

Reads:  /home/arduino/shackswitch_config.json
Writes: /home/arduino/shackswitch_config_new.json  (original untouched)

Run on Uno Q:
    python3 /home/arduino/ArduinoApps/first-app/python/migrate_config.py
"""

import json
import os
import sys

OLD_CONFIG = "/home/arduino/shackswitch_config.json"
NEW_CONFIG = "/home/arduino/shackswitch_config_new.json"

# Always scaffold all 16 ports — accommodates single or dual KK1L boards.
# port_count is managed via the UI setup page, not here.
MAX_PORTS = 16

ALL_BANDS = ["160m","80m","60m","40m","30m","20m","17m","15m","12m","10m","6m"]

# ---------------------------------------------------------------------------
# Default template entries for ports 1-8.
# These are deliberately illustrative — covering every capability type so a
# new builder understands the options just by reading the config.
# Ports 9-16 are disabled placeholders showing the same pattern.
# ---------------------------------------------------------------------------

DEFAULT_ANTENNAS = {
    # Multiband trapped vertical — shows multiple bands, ATU needed on some
    "1": {
        "name": "Trapped Vertical — e.g. Hustler 5BV",
        "enabled": True,
        "rx_bands":     ["160m", "80m", "40m", "20m", "15m", "10m"],
        "tx_bands":     ["40m", "20m", "15m", "10m"],
        "tx_atu_bands": ["160m", "80m"]
    },
    # Dedicated single-band — 80m
    "2": {
        "name": "80m Dipole",
        "enabled": True,
        "rx_bands":     ["80m"],
        "tx_bands":     ["80m"],
        "tx_atu_bands": []
    },
    # Dedicated single-band — 40m
    "3": {
        "name": "40m Dipole",
        "enabled": True,
        "rx_bands":     ["40m"],
        "tx_bands":     ["40m"],
        "tx_atu_bands": []
    },
    # Dedicated single-band — 20m
    "4": {
        "name": "20m Yagi",
        "enabled": True,
        "rx_bands":     ["20m"],
        "tx_bands":     ["20m"],
        "tx_atu_bands": []
    },
    # Dedicated single-band — 17m
    "5": {
        "name": "17m Vertical",
        "enabled": True,
        "rx_bands":     ["17m"],
        "tx_bands":     ["17m"],
        "tx_atu_bands": []
    },
    # Dedicated single-band — 15m
    "6": {
        "name": "15m Yagi",
        "enabled": True,
        "rx_bands":     ["15m"],
        "tx_bands":     ["15m"],
        "tx_atu_bands": []
    },
    # Dual-band — shows multiple bands without ATU requirement
    "7": {
        "name": "10m / 6m Vertical",
        "enabled": True,
        "rx_bands":     ["10m", "6m"],
        "tx_bands":     ["10m", "6m"],
        "tx_atu_bands": []
    },
    # Receive-only example — shows rx_bands only, tx lists empty
    "8": {
        "name": "Beverage RX Only — 160m / 80m",
        "enabled": True,
        "rx_bands":     ["160m", "80m"],
        "tx_bands":     [],
        "tx_atu_bands": []
    },
}

# Ports 9-16: disabled placeholders — structure mirrors enabled ports
# so a builder expanding to a second KK1L board knows exactly what to add.
for i in range(9, MAX_PORTS + 1):
    DEFAULT_ANTENNAS[str(i)] = {
        "name":         "",
        "enabled":      False,
        "rx_bands":     [],
        "tx_bands":     [],
        "tx_atu_bands": []
    }

# ---------------------------------------------------------------------------
# Default band map — covers all amateur bands, null where no antenna assigned.
# Builder replaces null with port number (1-16) for each band they cover.
# ---------------------------------------------------------------------------

DEFAULT_BAND_MAP = {
    "160m": 1,
    "80m":  2,
    "60m":  None,
    "40m":  3,
    "30m":  None,
    "20m":  4,
    "17m":  5,
    "15m":  6,
    "12m":  None,
    "10m":  7,
    "6m":   7
}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def normalise_band_map(band_map):
    """Fix mixed str/int values in band_map — all become int or None."""
    out   = {}
    fixes = []
    for band, port in band_map.items():
        if port is None:
            out[band] = None
        else:
            try:
                new_val = int(port)
                if not isinstance(port, int):
                    fixes.append((band, port, new_val))
                out[band] = new_val
            except (ValueError, TypeError):
                out[band] = None
                print(f"  WARNING: band_map[{band!r}] = {port!r} could not be converted — set to null")
    # Ensure all standard bands present
    for band in ALL_BANDS:
        if band not in out:
            out[band] = None
    return out, fixes


def merge_old_antennas(old_antennas, old_band_map):
    """
    Overlay old antenna names onto the default template.
    Capabilities are NOT migrated from old config (old names were placeholders).
    Builder must review and set rx_bands / tx_bands / tx_atu_bands manually.
    """
    antennas = {k: dict(v) for k, v in DEFAULT_ANTENNAS.items()}  # deep copy

    for ant_id, name in old_antennas.items():
        str_id = str(ant_id)
        if str_id in antennas and name:
            antennas[str_id]["name"] = name
            # Mark as enabled if it was in the old config
            antennas[str_id]["enabled"] = True

    return antennas


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def migrate():
    # --- Load existing config ---
    if not os.path.exists(OLD_CONFIG):
        print(f"ERROR: Cannot find {OLD_CONFIG}")
        sys.exit(1)

    with open(OLD_CONFIG, "r") as f:
        old = json.load(f)

    print(f"Loaded: {OLD_CONFIG}")
    print()

    # --- Extract values ---
    raw_band_map = old.get("band_map", {})
    old_antennas = old.get("antennas", {})

    # --- Normalise band_map types ---
    band_map, type_fixes = normalise_band_map(raw_band_map)

    if type_fixes:
        print("Type inconsistencies fixed in band_map:")
        for band, old_val, new_val in type_fixes:
            print(f"  {band}: {old_val!r} ({type(old_val).__name__}) → {new_val!r} (int)")
    else:
        print("band_map types: OK (no fixes needed)")
    print()

    # --- Build antenna table ---
    antennas = merge_old_antennas(old_antennas, band_map)

    print(f"Antenna slots ({MAX_PORTS} total — ports 1-8 illustrative defaults, 9-16 disabled):")
    for i in range(1, MAX_PORTS + 1):
        a      = antennas[str(i)]
        status = "enabled " if a["enabled"] else "disabled"
        print(f"  Port {i:2d} [{status}]: {a['name']!r}")
    print()
    print("  NOTE: Capabilities on ports 1-8 are illustrative defaults.")
    print("        Review ALL rx_bands / tx_bands / tx_atu_bands before going live.")
    print()

    # --- Hardware settings (top-level, unchanged) ---
    hardware_keys = [
        "input1_relay", "input2_relay",
        "input1_port",  "input2_port",
        "input1_label", "input2_label",
        "rfkit_ip",
    ]
    hardware = {k: old[k] for k in hardware_keys if k in old}

    # --- Warn about dropped keys ---
    known_keys = set(hardware_keys) | {"antennas", "band_map", "port_count"}
    dropped    = [k for k in old if k not in known_keys]
    if dropped:
        print(f"Keys dropped (not carried forward): {dropped}")
        print()

    # --- Assemble new config ---
    new_config = {
        "active_profile": "home",
        "profiles": {
            "home": {
                "description": "Home station — update with your callsign and location",
                "iaru_region": 1,
                "itu_zone":    28,
                "cq_zone":     14,
                "antennas":    antennas,
                "band_map":    band_map
            }
        },
        **hardware
    }

    # --- Write ---
    with open(NEW_CONFIG, "w") as f:
        json.dump(new_config, f, indent=2)

    print(f"New config written to: {NEW_CONFIG}")
    print(f"Original untouched:    {OLD_CONFIG}")
    print()
    print("Next steps:")
    print("  1. Review shackswitch_config_new.json")
    print("  2. Update 'description', iaru_region, itu_zone, cq_zone for your location")
    print("  3. Set correct antenna names for ports you are using")
    print("  4. Set enabled=true/false per port to match your hardware")
    print("  5. Correct rx_bands / tx_bands for each antenna's actual capability")
    print("  6. Populate tx_atu_bands where ATU is required for TX")
    print("  7. Update band_map — set port number (1-16) or null per band")
    print("  8. When happy:")
    print("       cp /home/arduino/shackswitch_config.json \\")
    print("          /home/arduino/shackswitch_config_backup.json")
    print("       cp /home/arduino/shackswitch_config_new.json \\")
    print("          /home/arduino/shackswitch_config.json")


if __name__ == "__main__":
    migrate()
