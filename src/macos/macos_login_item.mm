#include "macos/macos_login_item.h"

#import <ServiceManagement/ServiceManagement.h>

bool macos_login_item_enabled() {
    return SMAppService.mainAppService.status == SMAppServiceStatusEnabled;
}

bool macos_set_login_item_enabled(bool enabled) {
    NSError* error = nil;
    if (enabled == macos_login_item_enabled()) return true;
    return enabled ? [SMAppService.mainAppService registerAndReturnError:&error]
                   : [SMAppService.mainAppService unregisterAndReturnError:&error];
}
