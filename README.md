# Ark Survival Ascended — ArkShop Random Dino

This repository is a fork of the official [ArkServerApi/ASA-Plugins](https://github.com/ArkServerApi/ASA-Plugins) repository. Original projects, plugins, configurations, and build files are retained.

## ArkShop: weighted random dinosaur prizes

The ArkShop change adds an optional `Dinos` array to `Type: "dino"` shop items.

- Existing single-dinosaur entries continue to use their original top-level `Blueprint` and `Level` fields.
- When `Dinos` is present, one valid entry is selected using weighted random selection.
- Each entry's `MinLevel` and `MaxLevel` define an inclusive random level range for the spawned dinosaur.
- `Weight` is an optional positive integer and defaults to 1.
- `Gender`, `Neutered`, `SaddleBlueprint`, `PreventCryo`, `StryderHead`, `StryderChest`, and `GachaResources` can be set per entry. If omitted, the value from the parent shop item is used where applicable.

See [RandomDinoExample.json](ArkShop/Configs/RandomDinoExample.json) for a complete shop-item example.

**Level setting distinction:** the existing top-level shop-item `MinLevel` and `MaxLevel` remain buyer character-level restrictions. The new `MinLevel` and `MaxLevel` inside each `Dinos` array entry control the spawned dinosaur's level.

Invalid entries (missing Blueprint/level range, invalid levels, or invalid weights) are ignored. If no valid entries remain, the purchase is rejected before points are deducted. The selected dinosaur is passed through ArkShop's existing `GiveDino()` implementation, retaining its existing spawn, gender, saddle, cryopod, and special-dinosaur behavior.

## Upstream

Official source: [ArkServerApi/ASA-Plugins](https://github.com/ArkServerApi/ASA-Plugins)

All other source files are retained from the upstream fork. The functional code change is limited to ArkShop's dinosaur purchase flow; this README and the example configuration document that feature.
