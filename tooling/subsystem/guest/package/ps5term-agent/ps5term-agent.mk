# PS5Term GPL-3.0-or-later. The guest side of SSH-to-Linux sessions.
PS5TERM_AGENT_VERSION = 1
PS5TERM_AGENT_SITE = $(BR2_EXTERNAL_PS5TERM_PATH)/../guest-agent
PS5TERM_AGENT_SITE_METHOD = local
PS5TERM_AGENT_LICENSE = GPL-3.0-or-later
PS5TERM_AGENT_LICENSE_FILES = agent.c

define PS5TERM_AGENT_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -std=c11 -Wall -Wextra -Werror \
		$(@D)/agent.c -o $(@D)/ps5term-agent
endef

define PS5TERM_AGENT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/ps5term-agent $(TARGET_DIR)/usr/sbin/ps5term-agent
endef

$(eval $(generic-package))
