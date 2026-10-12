import unittest

from diagnostics.dust_gpu_target_guard import check_trace


def sufficient_events():
    return [
        {"kind": "ready"}, {"kind": "context_hooked"},
        {"kind": "small_candidate_scope", "selected_during_call": True},
        {"kind": "deferred_gpu_on_scratch", "same_dust_resource": True},
        {"kind": "summary", "gpu_dust": 10, "scratch_dust": 10,
         "other_dust": 0, "d3d9_small_candidates": 2},
    ]


class DustGpuTargetGuardTests(unittest.TestCase):
    def test_complete_dense_trace_passes(self):
        status = [{"render": {"no_gpu_draws": 8, "failed_draws": 0,
                              "begin_failures": 0, "end_failures": 0,
                              "tail_missing": 0, "d3d11_context_misses": 0,
                              "d3d11_thread_misses": 0}},
                  {"render": {"no_gpu_draws": 9, "failed_draws": 0,
                              "begin_failures": 0, "end_failures": 0,
                              "tail_missing": 0, "d3d11_context_misses": 0,
                              "d3d11_thread_misses": 0}}]
        self.assertEqual(check_trace(sufficient_events(), status)["result"], "pass")

    def test_same_dust_resource_on_rgba8_fails(self):
        events = sufficient_events() + [
            {"kind": "dust_gpu_outside_scratch", "same_dust_resource": True,
             "hostlong": 6429, "target_format": 28, "index_count": 3396}]
        self.assertEqual(check_trace(events)["outside_same_dust_resource"], 1)
        self.assertEqual(check_trace(events)["result"], "fail")

    def test_unexercised_path_fails(self):
        events = sufficient_events()
        events[-1]["d3d9_small_candidates"] = 0
        self.assertEqual(check_trace(events)["result"], "fail")

    def test_unselected_small_batch_fails(self):
        events = sufficient_events()
        events[2]["selected_during_call"] = False
        self.assertEqual(check_trace(events)["result"], "fail")

    def test_missing_deferred_gpu_draw_fails(self):
        events = [row for row in sufficient_events()
                  if row["kind"] != "deferred_gpu_on_scratch"]
        self.assertEqual(check_trace(events)["result"], "fail")

    def test_unreported_outside_gpu_count_fails(self):
        events = sufficient_events()
        events[-1]["other_dust"] = 1
        events[-1]["gpu_dust"] = 11
        self.assertEqual(check_trace(events)["result"], "fail")


if __name__ == "__main__":
    unittest.main()
