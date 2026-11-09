#pragma once
namespace torque {
// Log-only probe on the base Item function at vtable slot 132 (Item::_useOn).
// Lines go to torque_probe.txt in the launcher media folder.
bool installUseOnProbe();
void removeUseOnProbe();
} // namespace torque
