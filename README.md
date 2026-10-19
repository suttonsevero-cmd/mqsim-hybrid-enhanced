# MQSim-Hybrid: SLC/QLC SSD Simulator

MQSim-Hybrid extends [CMU-SAFARI/MQSim](https://github.com/CMU-SAFARI/MQSim) with a fixed SLC/QLC block partition, SLC-first data placement, valid-page migration from SLC to QLC, and additional correctness checks and statistics.

The implementation models an SLC region within a QLC-based SSD using configurable page counts and access latencies. It provides a base simulator for studying SLC buffering, migration overhead, garbage collection (GC), and foreground I/O performance. It does not include the separate LightGBM placement or online-refit experiments.

The comparison below uses upstream commit [`51f0f2d3fed92d88ef4a0fa61a38024b07bf9d16`](https://github.com/CMU-SAFARI/MQSim/tree/51f0f2d3fed92d88ef4a0fa61a38024b07bf9d16) as its reference.

## Changes from upstream MQSim

| Area | Upstream reference | This fork |
| --- | --- | --- |
| NAND technology | SLC, MLC, and TLC | Adds QLC and a fourth page-latency class |
| Hybrid storage | No explicit SLC/QLC block partition | Separate SLC and QLC blocks within each plane |
| Allocation | A user-data write frontier per stream and plane | Separate SLC and QLC user-data frontiers and free-block pools |
| Data placement | No SLC-first hybrid placement path | Foreground data writes prefer SLC and fall back to QLC when an SLC frontier cannot be allocated |
| SLC reclamation | No dedicated SLC-to-QLC eviction path | Migrates valid SLC pages to QLC, erases the victim, and returns it to the SLC pool |
| Capacity accounting | Uniform pages per block | Distinguishes nominal address slots from the writable capacity of SLC/QLC blocks |
| Command grouping | General multi-plane scheduling | Adds media-mode compatibility checks and fixes die scanning |
| Reporting | Original host, flash, mapping, and GC statistics | Adds latency percentiles, media-specific counters, SLC admission and eviction statistics, and eviction timing |
| Build | Original build files | Adds Windows/Unix selection to the Makefile |

The fork also fixes several mapping, GC, scheduling, and statistics problems described below. Some changes repair inherited behavior; others protect paths introduced by the hybrid extension. This does not imply that every issue reproduces under every upstream configuration.

## Hybrid storage and placement

### Fixed block partition

Each plane is divided into SLC and QLC block pools at initialization:

```text
SLC blocks = floor(blocks per plane * SLC_Block_Ratio / 100)
QLC blocks = blocks per plane - SLC blocks
```

SLC blocks use `SLC_Page_No_Per_Block`; QLC blocks use `Page_No_Per_Block`. The partition remains fixed during a simulation. The model does not dynamically convert blocks between SLC and QLC modes or place SLC and QLC on separate devices.

The usable page capacity per plane is:

```text
usable pages = SLC blocks * SLC pages per block
             + QLC blocks * QLC pages per block
```

Logical capacity and overprovisioning use this usable capacity. Physical-address encoding retains the nominal block/page geometry, with an additional SLC-mode flag.

For the supplied configuration, 512 blocks and a 10% SLC block ratio produce 51 SLC blocks and 461 QLC blocks per plane. With 256 and 1,024 pages per block, respectively, the usable capacity is 485,120 pages per plane. **The SLC block ratio is not the fraction of usable bytes stored in SLC.**

### Write and read paths

1. Foreground data writes reaching the FTL, including write-cache destaging, request an SLC page when hybrid mode is enabled.
2. The allocator uses the stream's SLC write frontier. If no SLC frontier can be allocated, the write falls back to the QLC frontier.
3. Rewrites invalidate the previous physical version through the mapping layer.
4. GC data migration and mapping-page writes use QLC destinations.
5. A first read without an existing mapping initializes a QLC-resident page and its metadata to represent pre-existing data; it does not submit a synthetic NAND PROGRAM.

Placement operates on flash-page transactions, not directly on whole host requests. A write acknowledged by the DRAM cache may reach this placement path later, after cache processing.

### SLC eviction

SLC eviction selects a safe candidate with the fewest valid pages, breaking ties by block ID. It excludes active write frontiers, blocks already undergoing reclamation, and blocks with outstanding program operations.

Valid pages are read from SLC and programmed into QLC. The SLC block is erased after its page movements complete, then returned to the SLC free-block pool. Ordinary QLC GC and QLC wear-leveling selection are kept separate from the SLC region.

At most one SLC eviction can be active per plane. Evictions also respect the existing per-plane GC/WL concurrency limit and QLC destination-space checks.

### Proactive eviction

**Proactive SLC eviction is enabled in the current source.** Its compile-time switch is in [`src/ssd/GC_and_WL_Unit_Base.h`](src/ssd/GC_and_WL_Unit_Base.h):

```cpp
static const bool ENABLE_PROACTIVE_SLC_EVICTION = true;
```

Set it to `false` and rebuild to disable proactive eviction. Passive eviction remains available when SLC allocation exhausts its free-block pool.

The proactive path samples SLC block consumption and estimates future demand using an exponentially weighted moving average. Its current source-level settings are:

| Setting | Value |
| --- | --- |
| Timer interval | 50 ms of simulated time |
| Demand prediction horizon | 100 ms |
| EWMA weight for the latest sample | 0.30 |
| Lower trigger bound | 10% of SLC blocks, rounded up |
| Upper trigger bound | 50% of SLC blocks, rounded up |
| Additional reserve | 2 SLC blocks |

The predicted demand plus the reserve is clamped to the trigger bounds. A check can request eviction when the free SLC block count is at or below this trigger; safety and destination-space checks still apply. Timer events are restarted by relevant completion events rather than kept alive indefinitely after work drains.

These are policy parameters, not bug fixes or workload-independent optimal settings. Report the proactive-eviction setting when comparing results.

## Correctness fixes and safeguards

### Mapping and request barriers

- **First-read metadata initialization.** A newly allocated page for a previously unmapped read now receives its physical LPA metadata. A temporary outstanding-program guard protects the reservation until mapping and metadata initialization finish. This prevents later GC from treating a valid page with `NO_LPA` metadata as a valid mapping-table index.
- **Metadata bounds checks.** GC checks for invalid or out-of-range data-page metadata before indexing the global mapping table. Mapping-consistency assertions remain enabled.
- **Barrier checks after space waits.** Writes resumed from an overfull-plane queue recheck whether their LPA has become GC-locked. Locked transactions move to the barrier queue instead of changing the mapping during migration.
- **Barrier rechecks during draining.** Read and write barrier queues stop draining if their LPA becomes locked again while waiting requests are being serviced.

### GC completion and resource management

- **Submit delayed erase transactions.** The deferred GC path now submits the ERASE transaction to the scheduler after outstanding user I/O drains. Previously, constructing the transaction without submitting it could leave the block locked and allow simulation events to run out before all host requests completed.
- **Track outstanding migration programs.** GC destination allocations increment outstanding-program bookkeeping, and completed normal migration programs decrement it. A closed destination block cannot be reclaimed while those programs are still pending.
- **Retry GC when forward progress stalls.** Space-blocked writes request a GC check. Write completion can also retry GC when foreground writes remain blocked and no reclamation is active.
- **Reserve QLC migration capacity.** Before starting QLC GC, SLC eviction, or static wear leveling, the simulator accounts for destination pages still required by active reclamations and space needed for write-frontier rotation. A nonempty free-block pool alone is not treated as sufficient capacity for another migration.
- **Keep block pools separate.** Erased blocks return to the pool matching their media mode. GC candidate checks exclude current frontiers and blocks already under reclamation; ordinary GC processes closed QLC blocks with invalid pages.
- **Protect capacity and endurance bookkeeping.** Hybrid capacity uses the actual writable page counts and wider capacity arithmetic. Configuration checks and erase-count/histogram bounds checks reject invalid states instead of allowing out-of-range access.

### Scheduling and NAND state

- **Correct `OUT_OF_ORDER` constructor arguments.** Suspension-enable flags and suspension-time parameters now reach the correct base-class arguments.
- **Fix suspension switch fall-through.** Missing `break` statements no longer cause eligible read/write suspension paths to fall into rejection branches.
- **Correct suspend/resume bookkeeping.** Suspending a die updates active-die counts; resume only processes suspended dies and restores command state and expected completion times.
- **Constrain multi-plane grouping.** A command cannot combine SLC and QLC transactions. Grouping also respects operation type, chip/die, page, and configured block-address constraints. Empty/null inputs and incompatible groups are checked.
- **Continue scanning other dies.** An unsuccessful dispatch attempt on one die no longer prematurely ends the search for work on subsequent dies.

### Statistics

- **Sparse latency histograms.** Percentiles retain microsecond bins and the nearest-rank definition, while storing only observed latency values. This avoids allocating a dense array proportional to the largest response time. Percentiles beyond the existing 32-bit microsecond API range raise an error instead of silently wrapping.
- **Separate NAND operation-time accounting.** Read, program, and erase execution time are accumulated under their respective operation types.
- **Additional diagnostics.** SLC bookkeeping counters expose residency mismatches and outstanding-program counter underflow.

## Configuration

Use [`ssdconfig.xml`](ssdconfig.xml) for the supplied hybrid configuration. The separate `config.xml` contains a different example configuration and does not enable hybrid mode.

The hybrid-specific flash parameters belong inside `<Flash_Parameter_Set>`:

```xml
<Flash_Technology>QLC</Flash_Technology>
<Enable_Hybrid_Mode>true</Enable_Hybrid_Mode>
<SLC_Block_Ratio>10</SLC_Block_Ratio>
<Page_No_Per_Block>1024</Page_No_Per_Block>
<SLC_Page_No_Per_Block>256</SLC_Page_No_Per_Block>
<Page_Capacity>16384</Page_Capacity>
<Page_Read_Latency_SLC>110000</Page_Read_Latency_SLC>
<Page_Program_Latency_SLC>160000</Page_Program_Latency_SLC>
```

| Parameter | Meaning |
| --- | --- |
| `Enable_Hybrid_Mode` | Enables the fixed SLC/QLC partition; `false` disables the SLC region |
| `SLC_Block_Ratio` | Percentage of blocks assigned to SLC, rounded down per plane |
| `SLC_Page_No_Per_Block` | Writable pages in an SLC block |
| `Page_No_Per_Block` | Pages in a QLC block and nominal page-address geometry |
| `Page_Read_Latency_SLC` / `Page_Program_Latency_SLC` | SLC operation latencies in nanoseconds |
| `Page_Read_Latency_TSB` / `Page_Program_Latency_TSB` | Fourth QLC page-class latencies, alongside LSB/CSB/MSB parameters |

The supplied `ssdconfig.xml` uses:

| Item | Value |
| --- | --- |
| Channels / chips per channel / dies per chip / planes per die | 8 / 4 / 1 / 2 |
| Blocks per plane | 512 |
| SLC block ratio | 10% |
| QLC / SLC pages per block | 1,024 / 256 |
| Page size | 16 KiB |
| QLC read / program latency | 110 us / 3,100 us for each configured page class |
| SLC read / program latency | 110 us / 160 us |
| Block erase latency | 3,500 us |
| Overprovisioning ratio | 0.204248046875 of usable physical capacity |
| Address mapping / ideal mapping table | `PAGE_LEVEL` / `true` |
| Transaction scheduler / GC victim policy | `OUT_OF_ORDER` / `GREEDY` |
| Preconditioning / GC copyback | Disabled / disabled |
| `Preemptible_GC_Enabled` | `false` |
| DRAM data-cache capacity | 256 MiB |
| Cache mode in the supplied `workload.xml` | `WRITE_CACHE` |

`Preemptible_GC_Enabled=false` selects the GC-priority queue-selection path. NAND command suspension has separate support and timing settings; this parameter is not a universal command-suspension switch.

Data-cache mode is configured per flow in `workload.xml`. Select `TURNED_OFF` to bypass the data cache or `WRITE_CACHE` to enable the write-cache path. This setting is separate from mapping-table caching.

These XML values are example simulation inputs, not specifications or validated measurements of a particular commercial SSD.

## Build and run

Build from the repository root with a C++11 compiler.

### Windows with MinGW-w64

Ensure `g++` and `mingw32-make` are on `PATH`:

```powershell
mingw32-make -j4
.\MQSim.exe -i .\ssdconfig.xml -w .\workload.xml
```

### Linux or other Unix-like systems

The Makefile includes a Unix branch:

```bash
make
./MQSim -i ssdconfig.xml -w workload.xml
```

Local build verification was performed with MinGW-w64 on Windows; the Unix build path has not been part of that verification.

After changing headers or compile-time switches, force a complete rebuild because the Makefile does not generate header dependency files:

```powershell
mingw32-make -B -j4
```

On Unix-like systems, use `make -B`.

The current executable waits for Enter after printing `Simulation complete; Press any key to exit.` For unattended use, remove the final `cin.get()` in `src/main.cpp` and rebuild, or provide end-of-file on standard input.

### Workload input

Before running, replace `<File_Path>` in `workload.xml` with your trace path. The checked-in example currently contains a local Windows path; trace datasets are not bundled with the source.

The trace reader expects five whitespace-separated columns:

```text
timestamp_ns device_id start_lba sector_count operation
```

Use `0` for writes and `1` for reads. LBAs and request sizes are in 512-byte sectors. Supply nondecreasing timestamps in nanoseconds; the reader consumes numeric timestamps directly, so changing the XML `Time_Unit` alone does not convert an input trace. Keep addresses within the configured logical capacity: the inherited reader folds oversized addresses, which can change workload locality.

## Output and interpretation

For a workload file named `workload.xml`, scenario results are written to `workload_scenario_1.xml`, with subsequent scenarios using their corresponding numbers.

Additional output includes:

- Overall and read/write P95/P99 device-response-time statistics, in microseconds.
- `SLC_Admission_Count`, `QLC_Admission_Count`, and `SLC_Admission_Ratio` for page allocations reaching the FTL data-write path.
- SLC proactive/passive eviction counts, completed eviction counts, valid pages selected for migration, and zero-movement evictions.
- Average eviction duration and selected-victim valid-page statistics.
- SLC and QLC transaction-latency counters and QLC page-class read counters.
- SLC residency mismatch and program-service underflow diagnostics.

Eviction timing covers selection through erase completion, including waiting within that interval. Transaction-level latency counters are distinct from host-request response times. Generic GC totals can include SLC reclamation because both paths share GC/WL infrastructure.

For each run, check that the generated and serviced host-request counts match. For queue statistics, check that enqueued and dequeued counts match after the simulation drains. A successful process exit or the final completion message alone is not sufficient evidence that all requests completed.

## Validation and current scope

Local checks covered clean MinGW builds and a 60,000-request stress trace with a reduced device geometry, read-only pages, repeated writes, and GC pressure. For this fork, all four combinations of `TURNED_OFF`/`WRITE_CACHE` and `Preemptible_GC_Enabled=false`/`true` completed all requests and drained their transaction queues. Residency-mismatch and SLC program-underflow counters remained zero.

A full `ali_22` trace regression also completed all 14,638,747 host requests and drained all 224 reported transaction queues in a patched-source validation build.

These are correctness checks, not a claim of performance improvement over upstream or validation of every parameter combination. Validation traces and the external validation scripts are not included in this source tree.

The main validated path is a single NVMe trace flow with `PAGE_LEVEL`, `Ideal_Mapping_Table=true`, `OUT_OF_ORDER`, `GREEDY`, and `Use_Copyback_for_GC=false`. Other schedulers, non-ideal mapping, multiple concurrent flows, and copyback require separate validation. `Address_Mapping=HYBRID` is an unrelated, unimplemented mapping option and is rejected; use `PAGE_LEVEL` for SLC/QLC storage.

QLC page classes use a `PageID % 4` latency model. This extension does not implement a complete vendor-specific QLC program sequence, dynamic SLC resizing, or a calibrated model of every commercial controller.

For reproducible experiments, preserve the source revision, compiler, XML files, trace conversion and units, cache mode, proactive-eviction switch, and random seeds. A seed alone should not be treated as a guarantee of bit-for-bit repeatability across builds or platforms.

## Source guide

| Files | Main changes |
| --- | --- |
| `src/exec/Flash_Parameter_Set.*`, `SSD_Device.cpp` | QLC/hybrid XML parameters, device construction, geometry, and validation |
| `src/nvm_chip/flash_memory/FlashTypes.h`, `Flash_Chip.*`, `Physical_Page_Address.h` | QLC type, media-dependent latencies, SLC address flag, and operation statistics |
| `src/ssd/Flash_Block_Manager*` | Separate block pools/frontiers, capacity bookkeeping, and outstanding-program tracking |
| `src/ssd/Address_Mapping_Unit*`, `src/utils/Logical_Address_Partitioning_Unit.*` | Hybrid capacity, address encoding, placement, first-read initialization, and barriers |
| `src/ssd/GC_and_WL_Unit*` | SLC eviction, safe victim selection, migration reservations, and completion/progress fixes |
| `src/ssd/TSU_Base.cpp`, `TSU_OutofOrder.cpp`, `NVM_PHY_ONFI_NVDDR2.cpp` | Command grouping, die scanning, and suspend/resume fixes |
| `src/host/IO_Flow_Base.*`, `src/ssd/Stats.*`, `FTL.cpp` | Percentiles, media/eviction statistics, and reporting |
| `Makefile` | Windows/Unix build commands |

## Upstream credit and citation

MQSim was developed by the SAFARI Research Group. This fork builds on that work and retains its MIT license and copyright notice. The upstream publication describes MQSim itself; it does not validate this hybrid extension.

Please cite the original MQSim paper when using this simulator in research:

> Arash Tavakkol, Juan Gomez-Luna, Mohammad Sadrosadati, Saugata Ghose, and Onur Mutlu. [MQSim: A Framework for Enabling Realistic Studies of Modern Multi-Queue SSD Devices](https://people.inf.ethz.ch/omutlu/pub/MQSim-SSD-simulation-framework_fast18.pdf). FAST, 2018.

```bibtex
@inproceedings{tavakkol2018mqsim,
  title={{MQSim: A Framework for Enabling Realistic Studies of Modern Multi-Queue SSD Devices}},
  author={Tavakkol, Arash and G{\'o}mez-Luna, Juan and Sadrosadati, Mohammad and Ghose, Saugata and Mutlu, Onur},
  booktitle={FAST},
  year={2018}
}
```

Also identify this fork and the exact revision used in your experimental methodology. See [LICENSE](LICENSE) for the MIT license text.
