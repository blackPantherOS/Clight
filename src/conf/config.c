#include <libconfig.h>
#include "config.h"

static void init_config_file(enum CONFIG file, char *filename);

static void init_config_file(enum CONFIG file, char *filename) {
    switch (file) {
        case LOCAL:
            if (getenv("XDG_CONFIG_HOME")) {
                snprintf(filename, PATH_MAX, "%s/clight.conf", getenv("XDG_CONFIG_HOME"));
            } else {
                snprintf(filename, PATH_MAX, "%s/.config/clight.conf", getpwuid(getuid())->pw_dir);
            }
            break;
        case GLOBAL:
            snprintf(filename, PATH_MAX, "%s/clight.conf", CONFDIR);
            break;
        default:
            break;
    }
}

int read_config(enum CONFIG file, char *config_file) {
    int r = 0;
    config_t cfg;
    const char *sensor_dev, *screendev, *sunrise, *sunset, *sensor_settings;

    if (!strlen(config_file)) {
        init_config_file(file, config_file);
    }
    if (access(config_file, F_OK) == -1) {
        WARN("Config file %s not found.\n", config_file);
        return -1;
    }

    config_init(&cfg);
    if (config_read_file(&cfg, config_file) == CONFIG_TRUE) {
        config_lookup_int(&cfg, "captures", &conf.num_captures);
        config_lookup_bool(&cfg, "no_smooth_backlight_transition", &conf.no_smooth_backlight);
        config_lookup_bool(&cfg, "no_smooth_gamma_transition", &conf.no_smooth_gamma);
        config_lookup_float(&cfg, "backlight_trans_step", &conf.backlight_trans_step);
        config_lookup_int(&cfg, "gamma_trans_step", &conf.gamma_trans_step);
        config_lookup_int(&cfg, "backlight_trans_timeout", &conf.backlight_trans_timeout);
        config_lookup_int(&cfg, "gamma_trans_timeout", &conf.gamma_trans_timeout);
        config_lookup_bool(&cfg, "no_backlight", &conf.no_backlight);
        config_lookup_bool(&cfg, "no_gamma", &conf.no_gamma);
        config_lookup_float(&cfg, "latitude", &conf.loc.lat);
        config_lookup_float(&cfg, "longitude", &conf.loc.lon);
        config_lookup_int(&cfg, "event_duration", &conf.event_duration);
        config_lookup_bool(&cfg, "no_dimmer", &conf.no_dimmer);
        config_lookup_float(&cfg, "dimmer_pct", &conf.dimmer_pct);
        config_lookup_float(&cfg, "shutter_threshold", &conf.shutter_threshold);
        config_lookup_bool(&cfg, "no_dpms", &conf.no_dpms);
        config_lookup_bool(&cfg, "verbose", &conf.verbose);
        config_lookup_bool(&cfg, "no_auto_calibration", &conf.no_auto_calib);
        config_lookup_bool(&cfg, "no_kdb_backlight", &conf.no_keyboard_bl);
        config_lookup_bool(&cfg, "gamma_long_transition", &conf.gamma_long_transition);
        config_lookup_bool(&cfg, "ambient_gamma", &conf.ambient_gamma);
        config_lookup_bool(&cfg, "no_screen", &conf.no_screen);
        config_lookup_float(&cfg, "screen_contrib", &conf.screen_contrib);
        config_lookup_int(&cfg, "screen_samples", &conf.screen_samples);
        config_lookup_bool(&cfg, "inhibit_autocalib", &conf.inhibit_autocalib);

        if (config_lookup_string(&cfg, "sensor_devname", &sensor_dev) == CONFIG_TRUE) {
            strncpy(conf.dev_name, sensor_dev, sizeof(conf.dev_name) - 1);
        }
        if (config_lookup_string(&cfg, "sensor_settings", &sensor_settings) == CONFIG_TRUE) {
            strncpy(conf.dev_opts, sensor_settings, sizeof(conf.dev_opts) - 1);
        }
        if (config_lookup_string(&cfg, "screen_sysname", &screendev) == CONFIG_TRUE) {
            strncpy(conf.screen_path, screendev, sizeof(conf.screen_path) - 1);
        }
        if (config_lookup_string(&cfg, "sunrise", &sunrise) == CONFIG_TRUE) {
            strncpy(conf.day_events[SUNRISE], sunrise, sizeof(conf.day_events[SUNRISE]) - 1);
        }
        if (config_lookup_string(&cfg, "sunset", &sunset) == CONFIG_TRUE) {
            strncpy(conf.day_events[SUNSET], sunset, sizeof(conf.day_events[SUNSET]) - 1);
        }

        config_setting_t *points, *root, *timeouts, *gamma;
        root = config_root_setting(&cfg);

        /* Load no_smooth_dimmer options */
        if ((points = config_setting_get_member(root, "no_smooth_dimmer_transition"))) {
            if (config_setting_length(points) == SIZE_DIM) {
                for (int i = 0; i < SIZE_DIM; i++) {
                    conf.no_smooth_dimmer[i] = config_setting_get_float_elem(points, i);
                }
            } else {
                WARN("Wrong number of no_smooth_dimmer_transition array elements.\n");
            }
        }
        
        /* Load dimmer_trans_steps options */
        if ((points = config_setting_get_member(root, "dimmer_trans_steps"))) {
            if (config_setting_length(points) == SIZE_DIM) {
                for (int i = 0; i < SIZE_DIM; i++) {
                    conf.dimmer_trans_step[i] = config_setting_get_float_elem(points, i);
                }
            } else {
                WARN("Wrong number of dimmer_trans_steps array elements.\n");
            }
        }
        
        /* Load dimmer_trans_timeouts options */
        if ((points = config_setting_get_member(root, "dimmer_trans_timeouts"))) {
            if (config_setting_length(points) == SIZE_DIM) {
                for (int i = 0; i < SIZE_DIM; i++) {
                    conf.dimmer_trans_timeout[i] = config_setting_get_int_elem(points, i);
                }
            } else {
                WARN("Wrong number of dimmer_trans_timeouts array elements.\n");
            }
        }
        
        /* Load regression points for backlight curve */
        int len;
        if ((points = config_setting_get_member(root, "ac_backlight_regression_points"))) {
            len = config_setting_length(points);
            if (len > 0 && len <= MAX_SIZE_POINTS) {
                conf.num_points[ON_AC] = len;
                for (int i = 0; i < len; i++) {
                    conf.regression_points[ON_AC][i] = config_setting_get_float_elem(points, i);
                }
            } else {
                WARN("Wrong number of ac_backlight_regression_points array elements.\n");
            }
        }

        /* Load regression points for backlight curve */
        if ((points = config_setting_get_member(root, "batt_backlight_regression_points"))) {
            len = config_setting_length(points);
            if (len > 0 && len <= MAX_SIZE_POINTS) {
                conf.num_points[ON_BATTERY] = len;
                for (int i = 0; i < len; i++) {
                    conf.regression_points[ON_BATTERY][i] = config_setting_get_float_elem(points, i);
                }
            } else {
                WARN("Wrong number of batt_backlight_regression_points array elements.\n");
            }
        }

        /* Load dpms timeouts */
        if ((timeouts = config_setting_get_member(root, "dpms_timeouts"))) {
            if (config_setting_length(timeouts) == SIZE_AC) {
                for (int i = 0; i < SIZE_AC; i++) {
                    conf.dpms_timeout[i] = config_setting_get_int_elem(timeouts, i);
                }
            } else {
                WARN("Wrong number of dpms_timeouts array elements.\n");
            }
        }

        /* Load capture timeouts while on battery -> +1 because EVENT is exposed too */
        if ((timeouts = config_setting_get_member(root, "ac_capture_timeouts"))) {
            if (config_setting_length(timeouts) == SIZE_STATES + 1) {
                for (int i = 0; i < SIZE_STATES + 1; i++) {
                    conf.timeout[ON_AC][i] = config_setting_get_int_elem(timeouts, i);
                }
            } else {
                WARN("Wrong number of ac_capture_timeouts array elements.\n");
            }
        }

        /* Load capture timeouts while on battery -> +1 because EVENT is exposed too */
        if ((timeouts = config_setting_get_member(root, "batt_capture_timeouts"))) {
            if (config_setting_length(timeouts) == SIZE_STATES + 1) {
                for (int i = 0; i < SIZE_STATES + 1; i++) {
                    conf.timeout[ON_BATTERY][i] = config_setting_get_int_elem(timeouts, i);
                }
            } else {
                WARN("Wrong number of batt_capture_timeouts array elements.\n");
            }
        }

        /* Load dimmer timeouts */
        if ((timeouts = config_setting_get_member(root, "dimmer_timeouts"))) {
            if (config_setting_length(timeouts) == SIZE_AC) {
                for (int i = 0; i < SIZE_AC; i++) {
                    conf.dimmer_timeout[i] = config_setting_get_int_elem(timeouts, i);
                }
            } else {
                WARN("Wrong number of dimmer_timeouts array elements.\n");
            }
        }

        /* Load gamma temperatures */
        if ((gamma = config_setting_get_member(root, "gamma_temp"))) {
            if (config_setting_length(gamma) == SIZE_STATES) {
                for (int i = 0; i < SIZE_STATES; i++) {
                    conf.temp[i] = config_setting_get_int_elem(gamma, i);
                }
            } else {
                WARN("Wrong number of gamma_temp array elements.\n");
            }
        }
        
        if ((timeouts = config_setting_get_member(root, "screen_timeouts"))) {
            if (config_setting_length(timeouts) == SIZE_AC) {
                for (int i = 0; i < SIZE_AC; i++) {
                    conf.screen_timeout[i] = config_setting_get_int_elem(timeouts, i);
                }
            } else {
                WARN("Wrong number of screen_timeouts array elements.\n");
            }
        }

    } else {
        WARN("Config file: %s at line %d.\n",
                config_error_text(&cfg),
                config_error_line(&cfg));
        r = -1;
    }
    config_destroy(&cfg);
    return r;
}

int store_config(enum CONFIG file) {
    int r = 0;
    config_t cfg;
    char config_file[PATH_MAX + 1] = {0};

    init_config_file(file, config_file);
    if (access(config_file, F_OK) != -1) {
        WARN("Config file %s already present. Overwriting.\n", config_file);
    }
    config_init(&cfg);

    config_setting_t *root = config_root_setting(&cfg);
    config_setting_t *setting = config_setting_add(root, "captures", CONFIG_TYPE_INT);
    config_setting_set_int(setting, conf.num_captures);

    setting = config_setting_add(root, "no_smooth_backlight_transition", CONFIG_TYPE_BOOL);
    config_setting_set_bool(setting, conf.no_smooth_backlight);

    setting = config_setting_add(root, "no_smooth_gamma_transition", CONFIG_TYPE_BOOL);
    config_setting_set_bool(setting, conf.no_smooth_gamma);

    setting = config_setting_add(root, "no_smooth_dimmer_transition", CONFIG_TYPE_ARRAY);
    for (int i = 0; i < SIZE_DIM; i++) {
        config_setting_set_bool_elem(setting, -1, conf.no_smooth_dimmer[i]);
    }

    setting = config_setting_add(root, "backlight_trans_step", CONFIG_TYPE_FLOAT);
    config_setting_set_float(setting, conf.backlight_trans_step);

    setting = config_setting_add(root, "gamma_trans_step", CONFIG_TYPE_INT);
    config_setting_set_int(setting, conf.gamma_trans_step);

    setting = config_setting_add(root, "dimmer_trans_steps", CONFIG_TYPE_ARRAY);
    for (int i = 0; i < SIZE_DIM; i++) {
        config_setting_set_float_elem(setting, -1, conf.dimmer_trans_step[i]);
    }

    setting = config_setting_add(root, "backlight_trans_timeout", CONFIG_TYPE_INT);
    config_setting_set_int(setting, conf.backlight_trans_timeout);

    setting = config_setting_add(root, "gamma_trans_timeout", CONFIG_TYPE_INT);
    config_setting_set_int(setting, conf.gamma_trans_timeout);

    setting = config_setting_add(root, "dimmer_trans_timeouts", CONFIG_TYPE_ARRAY);
    for (int i = 0; i < SIZE_DIM; i++) {
        config_setting_set_int_elem(setting, -1, conf.dimmer_trans_timeout[i]);
    }
    
    setting = config_setting_add(root, "gamma_long_transition", CONFIG_TYPE_BOOL);
    config_setting_set_bool(setting, conf.gamma_long_transition);
    
    setting = config_setting_add(root, "ambient_gamma", CONFIG_TYPE_BOOL);
    config_setting_set_bool(setting, conf.ambient_gamma);

    if (conf.loc.lat != LAT_UNDEFINED && conf.loc.lon != LON_UNDEFINED) {
        setting = config_setting_add(root, "latitude", CONFIG_TYPE_FLOAT);
        config_setting_set_float(setting, conf.loc.lat);
        setting = config_setting_add(root, "longitude", CONFIG_TYPE_FLOAT);
        config_setting_set_float(setting, conf.loc.lon);
    }

    setting = config_setting_add(root, "event_duration", CONFIG_TYPE_INT);
    config_setting_set_int(setting, conf.event_duration);

    setting = config_setting_add(root, "dimmer_pct", CONFIG_TYPE_FLOAT);
    config_setting_set_float(setting, conf.dimmer_pct);

    setting = config_setting_add(root, "verbose", CONFIG_TYPE_BOOL);
    config_setting_set_bool(setting, conf.verbose);

    setting = config_setting_add(root, "no_auto_calibration", CONFIG_TYPE_BOOL);
    config_setting_set_bool(setting, conf.no_auto_calib);

    setting = config_setting_add(root, "no_kdb_backlight", CONFIG_TYPE_BOOL);
    config_setting_set_bool(setting, conf.no_keyboard_bl);
    
    setting = config_setting_add(root, "inhibit_autocalib", CONFIG_TYPE_BOOL);
    config_setting_set_bool(setting, conf.inhibit_autocalib);

    setting = config_setting_add(root, "sensor_devname", CONFIG_TYPE_STRING);
    config_setting_set_string(setting, conf.dev_name);
    
    setting = config_setting_add(root, "sensor_settings", CONFIG_TYPE_STRING);
    config_setting_set_string(setting, conf.dev_opts);

    setting = config_setting_add(root, "screen_sysname", CONFIG_TYPE_STRING);
    config_setting_set_string(setting, conf.screen_path);

    setting = config_setting_add(root, "sunrise", CONFIG_TYPE_STRING);
    config_setting_set_string(setting, conf.day_events[SUNRISE]);

    setting = config_setting_add(root, "sunset", CONFIG_TYPE_STRING);
    config_setting_set_string(setting, conf.day_events[SUNSET]);

    setting = config_setting_add(root, "shutter_threshold", CONFIG_TYPE_FLOAT);
    config_setting_set_float(setting, conf.shutter_threshold);
    
    setting = config_setting_add(root, "screen_samples", CONFIG_TYPE_INT);
    config_setting_set_int(setting, conf.screen_samples);
    
    setting = config_setting_add(root, "screen_contrib", CONFIG_TYPE_FLOAT);
    config_setting_set_float(setting, conf.screen_contrib);

    /* -1 here below means append to end of array */
    setting = config_setting_add(root, "ac_backlight_regression_points", CONFIG_TYPE_ARRAY);
    for (int i = 0; i < conf.num_points[ON_AC]; i++) {
        config_setting_set_float_elem(setting, -1, conf.regression_points[ON_AC][i]);
    }

    setting = config_setting_add(root, "batt_backlight_regression_points", CONFIG_TYPE_ARRAY);
    for (int i = 0; i < conf.num_points[ON_BATTERY]; i++) {
        config_setting_set_float_elem(setting, -1, conf.regression_points[ON_BATTERY][i]);
    }

    setting = config_setting_add(root, "dpms_timeouts", CONFIG_TYPE_ARRAY);
    for (int i = 0; i < SIZE_AC; i++) {
        config_setting_set_int_elem(setting, -1, conf.dpms_timeout[i]);
    }

    setting = config_setting_add(root, "ac_capture_timeouts", CONFIG_TYPE_ARRAY);
    for (int i = 0; i < SIZE_STATES + 1; i++) {
        config_setting_set_int_elem(setting, -1, conf.timeout[ON_AC][i]);
    }

    setting = config_setting_add(root, "batt_capture_timeouts", CONFIG_TYPE_ARRAY);
    for (int i = 0; i < SIZE_STATES + 1; i++) {
        config_setting_set_int_elem(setting, -1, conf.timeout[ON_BATTERY][i]);
    }

    setting = config_setting_add(root, "dimmer_timeouts", CONFIG_TYPE_ARRAY);
    for (int i = 0; i < SIZE_AC; i++) {
        config_setting_set_int_elem(setting, -1, conf.dimmer_timeout[i]);
    }

    setting = config_setting_add(root, "gamma_temp", CONFIG_TYPE_ARRAY);
    for (int i = 0; i < SIZE_STATES; i++) {
        config_setting_set_int_elem(setting, -1, conf.temp[i]);
    }
    
    setting = config_setting_add(root, "screen_timeouts", CONFIG_TYPE_ARRAY);
    for (int i = 0; i < SIZE_AC; i++) {
        config_setting_set_int_elem(setting, -1, conf.screen_timeout[i]);
    }

    if(config_write_file(&cfg, config_file) != CONFIG_TRUE) {
        WARN("Failed to write new config to file.\n");
        r = -1;
    } else {
        INFO("New configuration successfully written to: %s\n", config_file);
    }
    config_destroy(&cfg);
    return r;
}
