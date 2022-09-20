#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include <stdio.h>
#include "bc_pwm.h"

typedef enum {
    EventTypeTick,
    EventTypeKey,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} PluginEvent;

typedef struct {
    int idx;
    pwmActor_t* pwm;
		float           duty;
    FuriTimer*      timer;    // the timer
    uint32_t        timerHz;  // system ticks per second
    int             fps;      // events/frames-per-second
} PluginState;

const float ctcssFreq[]  = {
    67, 69.3, 71, 71.9, 74.4, 77, 79.7, 82.5, 85.4, 88.5,
    91.5, 94.8, 97.4, 100, 103.5, 107.2, 110.9, 114.8, 118.8, 123,
    127.3, 131.8, 136.5, 141.3, 146.2, 151.4, 156.7, 159.8, 162.2,
    165.5, 167.9, 171.3, 173.8, 177.3, 179.9, 183.5, 186.2, 189.9,
    192.8, 196.6, 199.5, 203.5, 206.5, 210.7, 218.1, 225.7, 229.1,
    233.6, 241.8, 250.3, 254.1};

static void render_callback(Canvas* const canvas, void* ctx) {
    const PluginState* plugin_state = acquire_mutex((ValueMutex*)ctx, 25);
    if(plugin_state == NULL) {
        return;
    }
    // border around the edge of the screen
    canvas_draw_frame(canvas, 0, 0, 128, 64);
    canvas_set_font(canvas, FontPrimary);

    // Print the current hertz value -- convert int to string
    int length = snprintf(NULL, 0, "%0.1f", (double) ctcssFreq[plugin_state->idx]);
    char *currentHz = malloc(length + 1);
    snprintf(currentHz, length + 1, "%0.1f", (double) ctcssFreq[plugin_state->idx]);

//    char currentHz[10];
//    dtostrf(ctcssFreq[plugin_state->idx], 6, 1, currentHz);

    canvas_draw_str_aligned(
        canvas, 50, 30, AlignRight, AlignBottom, currentHz);

    canvas_draw_str_aligned(canvas, 120, 10, AlignRight, AlignTop, (plugin_state->pwm->run) ? "ON" : "OFF");

    release_mutex((ValueMutex*)ctx, plugin_state);
}

static void input_callback(InputEvent* input_event, FuriMessageQueue* event_queue) {
    furi_assert(event_queue);

    PluginEvent event = {.type = EventTypeKey, .input = *input_event};
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

static void evTick (PluginState* plugin_state)
{
  furi_assert(plugin_state);
	(void)0;
	return;
}

static void ctcss_state_init(PluginState* const plugin_state) {
    plugin_state->idx = 0;
    plugin_state->pwm = calloc(1, sizeof(*(plugin_state->pwm)));
    plugin_state->timer = NULL;
    plugin_state->timerHz = furi_kernel_get_tick_frequency();
    plugin_state->fps = 12;
    plugin_state->duty = 0.5;
}

static
void  cbTimer (FuriMessageQueue* queue)
{
	PluginEvent message = {.type = EventTypeTick};
	furi_message_queue_put(queue, &message, 0);
	return;
}

int32_t ctcss_app() {
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(PluginEvent));

    PluginState* plugin_state = malloc(sizeof(PluginState));

    ctcss_state_init(plugin_state);

    ValueMutex state_mutex;
    if(!init_mutex(&state_mutex, plugin_state, sizeof(PluginState))) {
        FURI_LOG_E("ctcss", "cannot create mutex\r\n");
        free(plugin_state);
        return 255;
    }

    // Set system callbacks
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, render_callback, &state_mutex);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    // Open GUI and register view_port
    Gui* gui = furi_record_open("gui");
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    PluginEvent event;
    pwmInit(plugin_state->pwm, PWM_ID_SPEAKER, 500, PWM_MODE_DUTY);
    plugin_state->timer = furi_timer_alloc(cbTimer, FuriTimerTypePeriodic, event_queue);
    // Don't currently need the timer
    //furi_timer_start(plugin_state->timer, plugin_state->timerHz);

    for(bool processing = true; processing;) {
        FuriStatus event_status = furi_message_queue_get(event_queue, &event, 100);
        PluginState* plugin_state = (PluginState*)acquire_mutex_block(&state_mutex);

        if(event_status == FuriStatusOk) {
            // press events
            if(event.type == EventTypeKey) {
                if(event.input.type == InputTypePress) {
                    switch(event.input.key) {
                    case InputKeyUp:
                        plugin_state->idx==37 ?
                          plugin_state->idx=0 :
                          plugin_state->idx++;
                        break;
                    case InputKeyDown:
                        plugin_state->idx==0 ?
                          plugin_state->idx=37 :
                          plugin_state->idx--;
                        break;
                    case InputKeyRight:
                        plugin_state->idx=37;
                        break;
                    case InputKeyLeft:
                        plugin_state->idx=0;
                        break;
                    case InputKeyOk:
                        if (plugin_state->pwm->run) {
                          (void)pwmStop(plugin_state->pwm);
                        } else {
                          pwmSet(plugin_state->pwm, ctcssFreq[plugin_state->idx], plugin_state->duty, true);  // true = start running now
                        }
                        break;
                    case InputKeyBack:
                        processing = false;
                        break;
                    }
                }
            } else if(event.type == EventTypeTick) {
                evTick(plugin_state);
            }
        } else {
            FURI_LOG_D("ctcss", "FuriMessageQueue: event timeout");
            // event timeout
        }

        view_port_update(view_port);
        release_mutex(&state_mutex, plugin_state);
    }

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close("gui");
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    delete_mutex(&state_mutex);

    return 0;
}
