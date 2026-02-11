///|
#borrow(s)
extern "C" fn c_serial_puts(s : Bytes) -> Unit = "moon_kernel_serial_puts"

///|
#borrow(s)
extern "C" fn c_vga_puts(s : Bytes) -> Unit = "moon_kernel_vga_puts"

///|
extern "C" fn c_get_ticks() -> Int = "moon_kernel_get_ticks"

///|
extern "C" fn c_keyboard_pop_event() -> Int = "moon_kernel_keyboard_pop_event"

///|
pub fn moon_kernel_entry() -> Unit {
  c_serial_puts(b"[moon] moon_kernel_entry start\n")
  c_vga_puts(b"[moon] Hello from MoonBit!\n")

  let _ticks = c_get_ticks()
  c_serial_puts(b"[moon] tick sample read\n")

  let event = c_keyboard_pop_event()
  if event != 0 {
    c_serial_puts(b"[moon] keyboard event dequeued\n")
  } else {
    c_serial_puts(b"[moon] keyboard queue empty\n")
  }

  c_serial_puts(b"[moon] moon_kernel_entry end\n")
}
