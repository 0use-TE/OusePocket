"""Generate the pinned TinyUSB audio-driver overlay; never edit managed sources."""
from pathlib import Path
import sys

def patched(source):
    changes = [
        (
            'TU_VERIFY(0 < tu_fifo_write_n(&audio->ep_out_ff, audio->lin_buf_out, n_bytes_received));',
            '// A legal empty packet, or a full software FIFO, must not stop OUT rearming.\n'
            '  if (n_bytes_received) {\n'
            '    (void) tu_fifo_write_n(&audio->ep_out_ff, audio->lin_buf_out, n_bytes_received);\n'
            '  }',
        ),
        (
            '// Schedule first feedback transmit\n            audiod_fb_send(func_id, false);',
            '// Defer first feedback until FIFO parameters and its nominal rate are ready.',
        ),
        (
            'TU_VERIFY(audiod_fb_params_prepare(func_id, alt));',
            'TU_VERIFY(audiod_fb_params_prepare(func_id, alt));\n'
            '      // Only a newly opened OUT interface needs its first feedback transfer.\n'
            '      if (audio->ep_fb != 0 && audio->ep_out_as_intf_num == itf) {\n'
            '        TU_VERIFY(audiod_fb_send(func_id, false));\n'
            '      }',
        ),
        (
            'audio->feedback.compute.fifo_count.nom_value = nominal;',
            'audio->feedback.compute.fifo_count.nom_value = nominal;\n'
            '        // The host needs a nonzero rate before it sends the first PCM packet.\n'
            '        audio->feedback.value = nominal;',
        ),
    ]
    for before, after in changes:
        if source.count(before) != 1:
            raise RuntimeError('Pinned TinyUSB audio source changed: ' + before)
        source = source.replace(before, after, 1)
    marker = 'static bool audiod_rx_xfer_isr(uint8_t rhport, audiod_function_t* audio, uint16_t n_bytes_received) {'
    if source.count(marker) != 1:
        raise RuntimeError('Pinned RX diagnostic hook changed')
    source = source.replace(marker, 'static _Atomic uint32_t ouse_rx_packets,ouse_rx_bytes;\n' + marker +
        '\n  atomic_fetch_add_explicit(&ouse_rx_packets, 1, memory_order_relaxed);'
        '\n  atomic_fetch_add_explicit(&ouse_rx_bytes, n_bytes_received, memory_order_relaxed);')
    source = '#include <stdatomic.h>\n' + source
    source += '\nvoid ouse_usb_driver_diag(uint32_t *packets,uint32_t *bytes,uint32_t *feedback) {\n' \
        '  *packets=atomic_load_explicit(&ouse_rx_packets,memory_order_relaxed);\n' \
        '  *bytes=atomic_load_explicit(&ouse_rx_bytes,memory_order_relaxed);\n' \
        '  *feedback=_audiod_fct[0].feedback.value;\n}\n'
    return source

if __name__ == '__main__':
    source, destination = map(Path, sys.argv[1:])
    result = patched(source.read_text(encoding='utf-8'))
    if not destination.exists() or destination.read_text(encoding='utf-8') != result:
        destination.write_text(result, encoding='utf-8', newline='\n')
