# pyevermizer

Python wrapper for Secret of Evermore Randomizer from
https://github.com/black-sliver/evermizer

This is used in [Archipelago cross-game multiworld randomizer](https://github.com/ArchipelagoMW/Archipelago).

## Import from package

* Install from PyPI via `pip install pyevermizer`
* Or download a [release](https://github.com/black-sliver/pyevermizer/releases) and `pip install` it
* Or clone, build and install a wheel from source using
  ```
  git clone https://github.com/black-sliver/pyevermizer --recurse-submodules
  python3 -m build --wheel
  pip install dist/*.whl
  ```

## Import from source

* Clone with submodules using
  ```
  git clone https://github.com/black-sliver/pyevermizer --recurse-submodules
  ```
* Simply import the cloned repo, it will auto-compile or run through cppyy.
  Either a C compiler or [cppyy](https://pypi.org/project/cppyy/) is required.

## API

```python
main(src: Path, dst: Path, placement: Path, apseed: str, apslot: str, seed: int, flags: str,
     money: int, exp: int, switches: list[str])  # create a randomized rom
get_locations() -> List[Location]  # returns a list of all non-sniff locations
get_sniff_locations() -> List[Location]  # returns a lof of all sniff spots
get_items() -> List[Item]  # returns a lost of all vanilla non-sniff items
get_sniff_items() -> List[Item]  # returns a list of vanilla sniff spot items
get_extra_items() -> List[Item]  # returns all extra items that can be placed, but are not vanilla
get_traps() -> List[Item]  # returns all traps that can be placed
get_logic() -> List[Location]  # returns the logic as real and pseudo locations for all locations that provide progress
P_...  # some progression IDs

class Location:
    name: str
    type: int  # location type, i.e. gourd, alchemy, boss
    index: int  # location index for each location type. (type, index) gives a unique ID
    difficulty: int  # difficulty 0..2 for bad/hidden spots
    requires: List[Tuple[int, int]]  # list of (amount, progression) required to reach the spot
    provides: List[Tuple[int, int]]  # list of (amount, progression) provided by reaching the spot

class Item:
    name: str
    progression: bool
    useful: bool
    type: int  # vanilla location type or extra location type, i.e. gourd, alchemy, boss, trap
    index: int  # item index for each location type. (type, index) gives a unique ID
    provides: List[Tuple[int, int]]  # list of (amount, progression) provided by obtaining the item
```

See Archipelago/worlds/soe for a complete example.
