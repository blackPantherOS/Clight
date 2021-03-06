#include "bus.h"

#define LOC_TIME_THRS 600                   // time threshold (seconds) before triggering location changed events (10mins)
#define LOC_DISTANCE_THRS 50000             // threshold for location distances before triggering location changed events (50km)

static int load_cache_location(void);
static void init_cache_file(void);
static int geoclue_init(void);
static int geoclue_get_client(void);
static int geoclue_hook_update(void);
static int on_geoclue_new_location(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int geoclue_client_start(void);
static void geoclue_client_delete(void);
static void cache_location(void);
static void publish_location(double new_lat, double new_lon, message_t *l);

static sd_bus_slot *slot;
static char client[PATH_MAX + 1], cache_file[PATH_MAX + 1];

DECLARE_MSG(loc_msg, LOC_UPD);
DECLARE_MSG(loc_req, LOCATION_REQ);

MODULE("LOCATION");

static void init(void) {
    init_cache_file();
    int r = geoclue_init();
    if (r == 0) {
        M_SUB(LOCATION_REQ);
        
        /*
         * timeout after 3s to check if geoclue2 gave us
         * any location. Otherwise, attempt to load it from cache
         */
        int fd = start_timer(CLOCK_MONOTONIC, 3, 0);
        m_register_fd(fd, true, NULL);
    } else {
        WARN("Failed to init.\n");
        if (load_cache_location() != 0) {
            /* small trick to notify GAMMA to stop as no location could be retrieved */
            state.current_loc.lat = LAT_UNDEFINED + 1;
            state.current_loc.lon = LON_UNDEFINED + 1;
        }
        m_poisonpill(self());
    }
}

static bool check(void) {
    /* It is only needed by GAMMA module that only works on X */
    return state.display && state.xauthority;
}

static bool evaluate(void) {
    /* 
     * Only start when no location and no fixed times for both events are specified in conf
     * AND GAMMA is enabled
     */
    return  !conf.no_gamma && 
            (conf.loc.lat == LAT_UNDEFINED || conf.loc.lon == LON_UNDEFINED) && 
            (!strlen(conf.day_events[SUNRISE]) || !strlen(conf.day_events[SUNSET]));
}

/*
 * Stop geoclue2 client and store latest location to cache.
 */
static void destroy(void) {
    if (strlen(client)) {
        geoclue_client_delete();
        cache_location();
    }
    /* Destroy this match slot */
    if (slot) {
        slot = sd_bus_slot_unref(slot);
    }
}

static void receive(const msg_t *const msg, UNUSED const void* userdata) {
    switch (MSG_TYPE()) {
    case FD_UPD:
        read_timer(msg->fd_msg->fd);
        if (state.current_loc.lat == LAT_UNDEFINED || state.current_loc.lon == LON_UNDEFINED) {
            load_cache_location();
        }
        break;
    case LOCATION_REQ: {
        loc_upd *l = (loc_upd *)MSG_DATA();
        if (VALIDATE_REQ(l)) {
            INFO("New location received: %.2lf, %.2lf.\n", l->new.lat, l->new.lon);
            // publish location before storing new location as state.current_loc is sent as "old" parameter
            publish_location(l->new.lat, l->new.lon, &loc_msg);
            memcpy(&state.current_loc, &l->new, sizeof(loc_t));
        } 
        break;
    }
    default:
        break;
    }
}

static int load_cache_location(void) {
    int ret = -1;
    FILE *f = fopen(cache_file, "r");
    if (f) {
        double new_lat, new_lon;
        if (fscanf(f, "%lf %lf\n", &new_lat, &new_lon) == 2) {
            publish_location(new_lat, new_lon, &loc_req);
            INFO("%.2lf %.2lf loaded from cache file!\n", new_lat, new_lon);
            ret = 0;
        }
        fclose(f);
    }
    if (ret != 0) {
        WARN("Error loading from cache file.\n");
    }
    return ret;
}

static void init_cache_file(void) {
    if (getenv("XDG_CACHE_HOME")) {
        snprintf(cache_file, PATH_MAX, "%s/clight", getenv("XDG_CACHE_HOME"));
    } else {
        snprintf(cache_file, PATH_MAX, "%s/.cache/clight", getpwuid(getuid())->pw_dir);
    }
}

/*
 * Init geoclue, then checks if a location is already available.
 */
static int geoclue_init(void) {
    int r = geoclue_get_client();
    if (r < 0) {
        goto end;
    }
    r = geoclue_hook_update();
    if (r < 0) {
        goto end;
    }
    r = geoclue_client_start();

end:
    if (r < 0) {
        WARN("Geoclue2 appears to be unsupported.\n");
    }
    /* In case of geoclue2 error, do not leave. Just disable this module */
    return -(r < 0);  // - 1 on error
}

/*
 * Store Client object path in client (static) global var
 */
static int geoclue_get_client(void) {
    SYSBUS_ARG(args, "org.freedesktop.GeoClue2", "/org/freedesktop/GeoClue2/Manager", "org.freedesktop.GeoClue2.Manager", "GetClient");
    return call(client, "o", &args, NULL);
}

/*
 * Hook our geoclue_new_location callback to PropertiesChanged dbus signals on GeoClue2 service.
 */
static int geoclue_hook_update(void) {
    SYSBUS_ARG(args, "org.freedesktop.GeoClue2", client, "org.freedesktop.GeoClue2.Client", "LocationUpdated");
    return add_match(&args, &slot, on_geoclue_new_location);
}

/*
 * On new location callback: retrieve new_location object,
 * then retrieve latitude and longitude from that object and store them in our conf struct.
 */
static int on_geoclue_new_location(sd_bus_message *m, UNUSED void *userdata, UNUSED sd_bus_error *ret_error) {
    /* Only if no conf location is set */
    if (conf.loc.lat == LAT_UNDEFINED && conf.loc.lon == LON_UNDEFINED) {
        const char *new_location, *old_location;
        sd_bus_message_read(m, "oo", &old_location, &new_location);

        double new_lat, new_lon;
    
        SYSBUS_ARG(lat_args, "org.freedesktop.GeoClue2", new_location, "org.freedesktop.GeoClue2.Location", "Latitude");
        SYSBUS_ARG(lon_args, "org.freedesktop.GeoClue2", new_location, "org.freedesktop.GeoClue2.Location", "Longitude");
        int r = get_property(&lat_args, "d", &new_lat, sizeof(new_lat)) + 
                get_property(&lon_args, "d", &new_lon, sizeof(new_lon));
        if (!r) {
            publish_location(new_lat, new_lon, &loc_req);
        }
    }
    return 0;
}

/*
 * Start our geoclue2 client after having correctly set needed properties.
 */
static int geoclue_client_start(void) {
    SYSBUS_ARG(call_args, "org.freedesktop.GeoClue2", client, "org.freedesktop.GeoClue2.Client", "Start");
    SYSBUS_ARG(id_args, "org.freedesktop.GeoClue2", client, "org.freedesktop.GeoClue2.Client", "DesktopId");
    SYSBUS_ARG(thres_args, "org.freedesktop.GeoClue2", client, "org.freedesktop.GeoClue2.Client", "DistanceThreshold");
    SYSBUS_ARG(time_args, "org.freedesktop.GeoClue2", client, "org.freedesktop.GeoClue2.Client", "TimeThreshold");
    SYSBUS_ARG(accuracy_args, "org.freedesktop.GeoClue2", client, "org.freedesktop.GeoClue2.Client", "RequestedAccuracyLevel");

    /* It now needs proper /usr/share/applications/clightc.desktop name */
    set_property(&id_args, 's', "clightc");
    set_property(&time_args, 'u', &(unsigned int) { LOC_TIME_THRS });
    set_property(&thres_args, 'u', &(unsigned int) { LOC_DISTANCE_THRS });
    set_property(&accuracy_args, 'u', &(unsigned int) { 2 }); // https://www.freedesktop.org/software/geoclue/docs/geoclue-gclue-enums.html#GClueAccuracyLevel -> GCLUE_ACCURACY_LEVEL_CITY
    return call(NULL, "", &call_args, NULL);
}

/*
 * Stop and delete geoclue2 client.
 */
static void geoclue_client_delete(void) {
    SYSBUS_ARG(stop_args, "org.freedesktop.GeoClue2", client, "org.freedesktop.GeoClue2.Client", "Stop");
    call(NULL, "", &stop_args, NULL);

    SYSBUS_ARG(del_args, "org.freedesktop.GeoClue2", "/org/freedesktop/GeoClue2/Manager", "org.freedesktop.GeoClue2.Manager", "DeleteClient");
    call(NULL, "", &del_args, "o", client);
}

static void cache_location(void) {
    if (state.current_loc.lat != LAT_UNDEFINED && state.current_loc.lon != LON_UNDEFINED) {
        FILE *f = fopen(cache_file, "w");
        if (f) {
            fprintf(f, "%lf %lf\n", state.current_loc.lat, state.current_loc.lon);
            DEBUG("Latest location stored in cache file!\n");
            fclose(f);
        } else {
            WARN("Caching location failed: %s.\n", strerror(errno));
        }
    }
}

static void publish_location(double new_lat, double new_lon, message_t *l) {
    l->loc.old.lat = state.current_loc.lat;
    l->loc.old.lon = state.current_loc.lon;
    l->loc.new.lat = new_lat;
    l->loc.new.lon = new_lon;
    M_PUB(l);
}
