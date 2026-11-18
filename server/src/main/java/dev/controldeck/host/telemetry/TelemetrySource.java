package dev.controldeck.host.telemetry;

import java.util.List;
import java.util.Map;

/**
 * A telemetry plugin (docs/03-SERVER.md): it finds the instances of its kind of part
 * on this machine and measures them. Each sample names its catalog entry; a source gives as
 * many instances as exist (every network card, later every GPU), and nothing for a part the
 * machine does not have.
 */
public interface TelemetrySource {

    /**
     * @param previous the readings before (null on the first sample: rates are then null)
     * @param current  the readings now
     * @param frame    the protocol-3 {@code stats} frame already computed from them
     */
    List<MetricCatalog.Sample> collect(HostMetrics.Reading previous, HostMetrics.Reading current,
                                       Map<String, Object> frame);
}
