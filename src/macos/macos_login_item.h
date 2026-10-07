#pragma once

// "Open at login" through SMAppService (macOS 13+). Only works from a bundle.
bool macos_login_item_enabled();
bool macos_set_login_item_enabled(bool enabled);
