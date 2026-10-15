#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Box.H>
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct Mode {
    int width;
    int height;
    int refresh; // in millihertz
};

struct Output {
    char name[64];
    int current_w;
    int current_h;
    int mode_count;
    Mode modes[32];
};

static Output outputs[4];
static int output_count = 0;

// UI Elements
static Fl_Choice *output_choice = nullptr;
static Fl_Hold_Browser *mode_browser = nullptr;
static Fl_Check_Button *save_check = nullptr;
static Fl_Box *status_box = nullptr;

// Reused parsing function
int get_outputs(Output *outs, int max_outputs) {
    FILE *fp = popen("wlr-randr", "r");
    if (!fp) return -1;

    char line[256];
    int output_idx = -1;

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;

        if (line[0] != '\0' && line[0] != ' ' && line[0] != '\t') {
            output_idx++;
            if (output_idx >= max_outputs) break;
            
            memset(&outs[output_idx], 0, sizeof(Output));
            sscanf(line, "%63s", outs[output_idx].name);
            
            char *curr = strstr(line, "current");
            if (curr) {
                sscanf(curr, "current %dx%d", &outs[output_idx].current_w, &outs[output_idx].current_h);
            }
        } 
        else if (output_idx >= 0) {
            int w = 0, h = 0;
            float hz = 60.0f;
            
            if (sscanf(line, " %d x %d px", &w, &h) == 2 || sscanf(line, " %dx%d", &w, &h) == 2) {
                char *hz_ptr = strstr(line, "@");
                if (hz_ptr) {
                    sscanf(hz_ptr, "@ %f", &hz);
                }
                
                int count = outs[output_idx].mode_count;
                if (count < 32 && w > 0 && h > 0) {
                    int exists = 0;
                    for (int m = 0; m < count; m++) {
                        if (outs[output_idx].modes[m].width == w && 
                            outs[output_idx].modes[m].height == h && 
                            outs[output_idx].modes[m].refresh == (int)(hz * 1000)) {
                            exists = 1;
                            break;
                        }
                    }
                    if (!exists) {
                        outs[output_idx].modes[count].width = w;
                        outs[output_idx].modes[count].height = h;
                        outs[output_idx].modes[count].refresh = (int)(hz * 1000);
                        outs[output_idx].mode_count++;
                    }
                }
            }
        }
    }

    pclose(fp);
    return output_idx + 1;
}

// Update modes callback
void update_modes(Fl_Widget *, void *) {
    mode_browser->clear();
    int sel = output_choice->value();
    if (sel < 0 || sel >= output_count) return;

    Output *out = &outputs[sel];
    char buf[128];
    for (int i = 0; i < out->mode_count; i++) {
        snprintf(buf, sizeof(buf), "%dx%d @ %.2f Hz", 
                 out->modes[i].width, 
                 out->modes[i].height, 
                 (float)out->modes[i].refresh / 1000.0f);
        mode_browser->add(buf);
    }
    if (out->mode_count > 0) {
        mode_browser->select(1);
    }
}

// Function to write/update persistent autostart configuration
void save_permanently(const char *output_name, int width, int height, float hz) {
    const char *home = getenv("HOME");
    if (!home) return;
    
    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "%s/.config/labwc", home);
    
    char mkdir_cmd[600];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p \"%s\"", dir_path);
    (void)system(mkdir_cmd);
    
    char autostart_path[512];
    snprintf(autostart_path, sizeof(autostart_path), "%s/autostart", dir_path);
    
    // Read existing file content, filtering out any old wlr-randr command for this specific output
    FILE *f = fopen(autostart_path, "r");
    char content[4096] = "";
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "wlr-randr") && strstr(line, output_name)) {
                continue; // Skip duplicate/old configuration for this monitor
            }
            strncat(content, line, sizeof(content) - strlen(content) - 1);
        }
        fclose(f);
    }
    
    // Append the new command
    char new_cmd[256];
    snprintf(new_cmd, sizeof(new_cmd), "wlr-randr --output %s --mode %dx%d@%.3f &\n", 
             output_name, width, height, hz);
    strncat(content, new_cmd, sizeof(content) - strlen(content) - 1);
    
    // Write back to autostart
    f = fopen(autostart_path, "w");
    if (f) {
        if (content[0] != '#') {
            fprintf(f, "#!/bin/sh\n");
        }
        fprintf(f, "%s", content);
        fclose(f);
        
        char chmod_cmd[600];
        snprintf(chmod_cmd, sizeof(chmod_cmd), "chmod +x \"%s\"", autostart_path);
        (void)system(chmod_cmd);
    }
}

// Callback for Apply button
void apply_cb(Fl_Widget *, void *) {
    int out_sel = output_choice->value();
    int mode_sel = mode_browser->value();

    if (out_sel < 0 || out_sel >= output_count) {
        status_box->label("Error: No output selected.");
        return;
    }
    if (mode_sel <= 0) {
        status_box->label("Error: No mode selected.");
        return;
    }

    Output *out = &outputs[out_sel];
    Mode *mode = &out->modes[mode_sel - 1];

    char cmd[256];
    float hz = (float)mode->refresh / 1000.0f;
    snprintf(cmd, sizeof(cmd), "wlr-randr --output %s --mode %dx%d@%.3f", 
             out->name, mode->width, mode->height, hz);

    int ret = system(cmd);
    if (ret == 0) {
        if (save_check->value()) {
            save_permanently(out->name, mode->width, mode->height, hz);
            status_box->label("Applied & saved to Labwc autostart!");
        } else {
            status_box->label("Resolution applied successfully!");
        }
    } else {
        status_box->label("Failed to apply resolution.");
    }
}

int main(void) {
    output_count = get_outputs(outputs, 4);
    if (output_count <= 0) {
        fprintf(stderr, "No active outputs found or wlr-randr missing.\n");
        return 1;
    }

    // Expanded window height to fit checkbox
    Fl_Window *window = new Fl_Window(360, 360, "Labwc Resolution Switcher");

    // Output Dropdown Selection
    output_choice = new Fl_Choice(20, 35, 320, 25, "Select Output:");
    output_choice->align(FL_ALIGN_TOP_LEFT);
    for (int i = 0; i < output_count; i++) {
        char label[128];
        snprintf(label, sizeof(label), "%s (Current: %dx%d)", outputs[i].name, outputs[i].current_w, outputs[i].current_h);
        output_choice->add(label);
    }
    output_choice->value(0);
    output_choice->callback(update_modes);

    // Compatible Modes Scroll Browser
    mode_browser = new Fl_Hold_Browser(20, 95, 320, 130, "Compatible Modes:");
    mode_browser->align(FL_ALIGN_TOP_LEFT);

    // Persistent Checkbox
    save_check = new Fl_Check_Button(20, 235, 320, 25, "Save permanently (Labwc autostart)");

    // Apply Button
    Fl_Button *apply_btn = new Fl_Button(20, 270, 320, 30, "Apply Resolution");
    apply_btn->callback(apply_cb);

    // Status Message Box
    status_box = new Fl_Box(20, 310, 320, 25, "");
    status_box->labelsize(12);

    // Populate initial modes
    update_modes(nullptr, nullptr);

    window->end();
    window->show();

    return Fl::run();
}
