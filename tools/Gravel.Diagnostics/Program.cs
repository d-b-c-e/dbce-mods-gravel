using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using Dbce.Wheel.Recording;

static class Program {
    static int Main(string[] args) {
        try {
            if (args.Length != 3 || args[0] != "inspect") throw new ArgumentException("Usage: inspect <signals.jsonl> <capture-id>");
            SessionMetadata metadata = null; SessionFooter footer = null;
            long rows = 0, vehicleRows = 0, forceRows = 0, overflowRows = 0, invalidRows = 0;
            double last = -1, maxGap = 0; bool timedOut = false;
            var summaries = new Dictionary<string, long>();
            foreach (var record in SessionReader.Read(args[1])) {
                if (record.Kind == SessionRecordKind.Metadata) {
                    metadata = record.Metadata;
                    if (metadata.Game != "Gravel" || metadata.PluginVersion != "native-signals-1" ||
                        metadata.Properties["producer"] != "gravel-native-observer-v1" ||
                        metadata.Properties["kind"] != "sampled-signals" || metadata.Properties["captureId"] != args[2])
                        throw new InvalidDataException("Wrong producer, kind or capture identity.");
                }
                if (record.Kind == SessionRecordKind.Footer) footer = record.Footer;
                if (record.Kind != SessionRecordKind.Sample) continue;
                var s = record.Sample; var c = s.Channels; rows++;
                if (c["delivery.muted"] != 1) throw new InvalidDataException("Capture must claim the diagnostic mute.");
                if (c["ue.live"] == 1) vehicleRows++;
                if (c["ffb.observed"] == 1) forceRows++;
                if (c["ffb.tableOverflow"] != 0) overflowRows++;
                if (c.Any(v => v.Key.EndsWith(".invalid", StringComparison.Ordinal) && v.Value != 0)) invalidRows++;
                if (c["capture.startupTimedOut"] != 0) timedOut = true;
                if (last >= 0) maxGap = Math.Max(maxGap, s.ElapsedSeconds-last);
                last = s.ElapsedSeconds;
                foreach (var key in new[] {"ffb.constantSummary", "ffb.periodicSummary", "ffb.springSummary", "ffb.damperSummary"}) {
                    if (c.ContainsKey(key)) summaries[key] = summaries.GetValueOrDefault(key) + 1;
                    else if (c[key+".observed"] != 0 && c.GetValueOrDefault(key+".invalid") != 1) throw new InvalidDataException("Observed force missing without a value or invalid flag.");
                }
            }
            if (metadata == null || footer == null || !footer.Completed || rows == 0) throw new InvalidDataException("Capture did not complete normally.");
            Console.WriteLine(JsonSerializer.Serialize(new {
                status="complete-sampled-signals", captureId=args[2], samples=rows, elapsedSeconds=last,
                vehicleRows, forceRows, overflowRows, invalidRows, startupTimedOut=timedOut, maxSampleGapSeconds=maxGap,
                summaryRows=summaries, trajectoryQualified=false, forceReplayQualified=false, hardwareMuteQualified=false,
                limitation="Polled request summaries; no physics tick, ordered effect/collision lifecycle, offline-stage gate or pose owner."
            },new JsonSerializerOptions {WriteIndented=true}));
            return 0;
        } catch (Exception error) { Console.Error.WriteLine(error.Message); return 1; }
    }
}
