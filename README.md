# libspanlog

Binary coordination protocol library for distributed log segment management.

## Overview

libspanlog implements a binary wire protocol for coordinating segment lifecycle
operations across distributed log storage nodes. It handles session negotiation,
channel-multiplexed concurrent access, streaming reads and appends, lease-based
access control, and live segment migration for cluster rebalancing.

## Architecture

The library is organized into 14 subsystems:

- **protocol** — Wire format parsing, nested frame validation, CRC-32C
- **session** — Session lifecycle, epoch management, capability negotiation
- **channel** — Logical channel multiplexing, slot allocation
- **stream** — Ordered byte-stream abstraction, fragment reassembly
- **span** — Span registry, metadata management, lifecycle FSM
- **segment** — Storage segment allocation, compaction, GC
- **lease** — Time-bounded access leases, renewal, generation-tracked expiry
- **cursor** — Read cursor positions, segment caching, invalidation
- **replication** — Replica set management, quorum tracking, sync
- **migration** — Live span migration orchestration, transfer, handoff
- **checkpoint** — Durable checkpoint/snapshot coordination
- **retention** — Retention policy evaluation, watermark tracking
- **route** — Route table mapping spans to nodes/partitions
- **backpressure** — Credit-based flow control, per-partition windows

## Building

```
make            # build static library
make test       # run basic tests
make poc        # build proof-of-concept programs
make fuzzers    # build fuzz harnesses (requires clang with libFuzzer)
make asan       # build with AddressSanitizer
```

## Wire Protocol

Messages use a nested, length-prefixed, CRC-validated format:

```
[Session Header: 18 bytes]
  magic(4) | version(2) | session_id(4) | epoch(4) | frame_count(2) | header_crc(4)

[Frame: variable]
  length(2) | type(1) | sequence(2) | session_id(4) | channel_id(2) |
  route_id(2) | owner_token(4) | flags(2) | sub_count(1) |
  [Subrecord]*N | payload_crc(4)

[Subrecord: variable]
  kind(1) | length(2) | local_epoch(4) | generation(4) | lease_token(4) |
  [payload: length bytes]
```

## License

MIT — see LICENSE file.
