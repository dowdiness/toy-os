extern "C" fn c_serial_puts(s : Bytes) -> Unit = "moon_kernel_serial_puts"
extern "C" fn c_vga_puts(s : Bytes) -> Unit = "moon_kernel_vga_puts"

pub fn moon_kernel_entry() -> Unit {
  c_serial_puts(b"[moon] moon_kernel_entry start\n")
  c_vga_puts(b"[moon] Hello from MoonBit!\n")
  c_serial_puts(b"[moon] moon_kernel_entry end\n")
}
