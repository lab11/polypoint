PolyPoint Apps
==============

The PolyPoint app has undergone a few significant revisions. To facilitate
parallel testing and development, it's been forked (copy a folder and go) a few
times.

* `pp_oneway_[common,tag,anchor]` -- This version revises the ranging protocol.
  The original protocol was `NUM_MESUREMENTS` * {Tag `--TAG_POLL-->` Anchor,
  `NUM_ANCHORS` * {Anchor `--ANC_RESP-->` Tag}} + {Tag `--TAG_FINAL-->` Anchor,
  `NUM_ANCHORS` * {Anchor `--ANC_FINAL-->` Tag}}. The revised protocol sends
  `NUM_MESUREMENTS` * {Tag `--TAG_POLL-->` Anchor}, `NUM_ANCHORS` * {Anchor
  `--ANC_FINAL-->` Tag}. This is `NUM_MESUREMENTS` * `NUM_ANCHORS` fewer packets.

* `polypoint_[common,tag,anchor]` -- This version split tags and anchors into
  two dedicated applications, with some common code. It uses the same
  ranging protocol as the previous version. It ups the UWB baud rate to
  6.2 Mbaud and tightens transmission timings to near minimum, largely by
  manually measuring the duration of each event. It adds an option to compute
  the 10th percentile estimate on the anchor during the ranging event,
  significantly reducing the length of the final anchor message and the amount
  of data the tag needs to offload. This app improved the update rate from
  about 0.25 Hz to 3 Hz, or up to 4.5 Hz with `ANC_FINAL_PERCENTILE_ONLY`.

* `dw1000_test` -- The app as run during the Microsoft Indoor Localization
  Competition 2015. This was a unified application, with `#define`'s selecting
  anchors versus tags.
