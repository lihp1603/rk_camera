all:
	$(call all-subdir-makefiles)

define all-subdir-makefiles
$(call all-makefiles-under,$(CURDIR))
endef

define all-makefiles-under
$(call build-subdir-makefile, $(wildcard $(1)/*/Android.mk))
endef


define build-subdir-makefile
    @for makefile in $1; \
    do \
        echo "making $@ in $$makefile"; \
        ( cd `dirname $$makefile` && $(MAKE) -f Android.mk  ) \
        || exit 1; \
    done
endef
