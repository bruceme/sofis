#include <stdint.h>

#include "SDL_surface.h"
#include "SDL_timer.h"
#include "base-gauge.h"
#include "generic-layer.h"
#include "map-gauge.h"
#include "map-math.h"
#include "map-tile-provider.h"
#include "misc.h"
#include "sdl-colors.h"
#include "res-dirs.h"

/*Each tile is 256x256 px*/
#define TILE_SIZE 256
/*Time after which the viewport re-ties to the marker*/
#define MANIPULATE_TIMEOUT 2000
/* Scroll when the marker bouding box reaches this limit around the viewport*/
#define PIX_LIMIT 10

#define map_gauge_marker_left(self) ((self)->marker.x - generic_layer_w(&(self)->marker.layer)/2)
#define map_gauge_marker_top(self) ((self)->marker.y - generic_layer_h(&(self)->marker.layer)/2)

#define map_gauge_marker_worldbox(self) (SDL_Rect){ \
    .x = map_gauge_marker_left(self), \
    .y = map_gauge_marker_top(self), \
    .w = generic_layer_w(&(self)->marker.layer), \
    .h = generic_layer_h(&(self)->marker.layer) \
}


#define map_gauge_viewport(self) (SDL_Rect){ \
    .x = (self)->world_x, \
    .y = (self)->world_y, \
    .w = base_gauge_w(BASE_GAUGE((self))), \
    .h = base_gauge_h(BASE_GAUGE((self))) \
}


static void map_gauge_render(MapGauge *self, Uint32 dt, RenderContext *ctx);
static void map_gauge_update_state(MapGauge *self, Uint32 dt);
static MapGauge *map_gauge_dispose(MapGauge *self);
static BaseGaugeOps map_gauge_ops = {
   .render = (RenderFunc)map_gauge_render,
   .update_state = (StateUpdateFunc)map_gauge_update_state,
   .dispose = (DisposeFunc)map_gauge_dispose
};

/**
 * @brief Creates a new MapGauge of given dimensions.
 *
 * Caller is responsible for freeing the gauge by calling
 * base_gauge_free.
 *
 * @param w Width (in pixels) of the gauge
 * @param h Height (in pixels) of the gauge
 * @return a newly-allocated MapGauge on success, NULL on failure.
 *
 * @see base_gauge_free
 */
MapGauge *map_gauge_new(int w, int h)
{
    MapGauge *rv;

    rv = calloc(1, sizeof(MapGauge));
    if(rv){
        if(!map_gauge_init(rv,w,h))
            return base_gauge_free(BASE_GAUGE(rv));
    }
    return rv;
}

/**
 * @brief Inits an already allocated MapGauge with given dimensions.
 *
 * This function must not be called twice on the same object
 * without calling map_gauge_dispose inbetween.
 *
 * @param self a MapGauge
 * @param w Width (in pixels) of the gauge
 * @param h Height (in pixels) of the gauge
 * @return @p self on success, NULL on failure.
 *
 * @see map_gauge_dispose
 * @see map_gauge_new
 */
MapGauge *map_gauge_init(MapGauge *self, int w, int h)
{
    int twidth, theight;
    size_t cache_tiles;

    twidth = w / TILE_SIZE;
    theight = h /TILE_SIZE;
    /* Worst case is the view centered on the junction of 4 tiles
     * multiplied by the number of tiles the view can see at once.
     * with a minimum of 1 if the view is smaller than a tile
     */
    cache_tiles = (MAX(twidth, 1) * MAX(twidth, 1)) * 4;

    /*Keep in the tile stack 2 viewports worth of tiles*/
#if HAVE_IGN_OACI_MAP
    self->tile_providers[self->ntile_providers++] = map_tile_provider_new(
        MAPS_HOME"/ign-oaci", "jpg",
        cache_tiles*2
    );
#else
    self->tile_providers[self->ntile_providers++] = map_tile_provider_new(
        MAPS_HOME"/osm-aip", "png",
        cache_tiles*2
    );
#endif
    self->tile_providers[self->ntile_providers++] = map_tile_provider_new(
        MAPS_HOME"/osm", "png",
        cache_tiles*2
    );
    /*TODO: Scale the plane relative to the gauge's size*/
    generic_layer_init_from_file(&self->marker.layer, IMG_DIR"/plane32.png");
    generic_layer_build_texture(&self->marker.layer);

    base_gauge_init(BASE_GAUGE(self),
        &map_gauge_ops,
        w, h
    );

    return self;
}

/**
 * @brief Release any resources internally held by the MapGauge
 *
 * This function always returns NULL (convenience behavior).
 *
 * @param self a MapGauge
 * @return NULL
 */
static MapGauge *map_gauge_dispose(MapGauge *self)
{
    for(int i = 0; i < self->state.npatches; i++)
        generic_layer_unref(self->state.patches[i].layer);
    if(self->state.patches)
        free(self->state.patches);

    generic_layer_dispose(&self->marker.layer);
    for(int i = 0; i < self->ntile_providers; i++)
        map_tile_provider_free(self->tile_providers[i]);

    return self;
}

/**
 * @brief Sets the current zoom level show by the gauge. Valid levels are
 * 0 to MAP_GAUGE_MAX_LEVEL, owing to types internally used to store positions.

 * TODO: ATM This is 15 due to SDL_Rect(int) usage, should be 16 with uint32_t used
 * everywhere else. Fix by creating a SDLExt_URect using Uint32 + Intersection function
 *
 * This function will try its best to keep the current area and zoom on it.
 *
 * @param self a MapGauge
 * @param level the level to show
 * @return true on success, false on failure (level unsupported, ...)
 */
bool map_gauge_set_level(MapGauge *self, uintf8_t level)
{
    double lat, lon;
    uint32_t new_x, new_y;

    if(level > 15)
        return false;
    if(level != self->level){
        /* Keep the view at the same place. TODO: There should be a way to do the
         * same without having to go through geo coords transforms*/
        map_math_pixel_to_geo(self->world_x, self->world_y, self->level, &lat, &lon);
        map_math_geo_to_pixel(lat, lon, level, &new_x, &new_y);
        /*Same for the marker*/
        map_math_pixel_to_geo(self->marker.x, self->marker.y, self->level, &lat, &lon);
        self->level = level;
        map_gauge_set_viewport(self, new_x, new_y, false);
        map_gauge_set_marker_position(self, lat, lon);
    }
    return true;
}

/**
 * @brief Updates the marker position
 *
 * Client code should use this function to make the marker move.
 *
 * @param self a MapGauge
 * @param latitude The new latitude of the marker
 * @param longitude The new longitude of the marker
 * @return true on success, false on failure.
 */
bool map_gauge_set_marker_position(MapGauge *self, double latitude, double longitude)
{
    bool rv;
    uint32_t new_x,new_y;

    /* TODO: This is purely based on time and should not be in this function
     * it should be some kind of animation or use another system to have
     * time-based events
     * */
    if(self->roaming && SDL_GetTicks() - self->last_manipulation > MANIPULATE_TIMEOUT){
        self->roaming = false;
        map_gauge_center_on_marker(self, true);
    }

    rv = map_math_geo_to_pixel(latitude, longitude, self->level, &new_x, &new_y);
    if(new_x != self->marker.x || new_y != self->marker.y){
        self->marker.x = new_x;
        self->marker.y = new_y;
        if(!self->roaming){
            map_gauge_follow_marker(self);
        }
        BASE_GAUGE(self)->dirty = true;
        return true;
    }
    return false;
}

/**
 * @brief Updates the marker heading (dregrees, 0-360). If the value
 * is outside the valid range, it will be clamped to it.
 *
 * Client code should use this function to make the marker face
 * the direction it's heading towards.
 *
 * @param self a MapGauge
 * @param heading The new heading of the marker, in degrees
 * @return true on success, false on failure.
 */
bool map_gauge_set_marker_heading(MapGauge *self, float heading)
{
    heading = clampf(heading, 0, 360);
    if(heading != self->marker.heading){
        self->marker.heading = heading;
        BASE_GAUGE(self)->dirty = true;
        return true;
    }
    return false;
}

/**
 * @brief Moves the viewport by the given increment while putting it in a
 * temporary 'roaming' mode. Roaming mode will last MANIPULATE_TIMEOUT ms
 * after the last call to map_gauge_manipulate_viewport.
 *
 * While in roaming mode, the viewport is free to roam the entire map without
 * being dragged back when the marker moves. Once the roaming mode expires, the
 * viewport automatically goes back centered on the marker position.
 *
 * This function is intended to be called by 'client' code.
 *
 * @param self a MapGauge
 * @param dx increment for the x position, positive or negative
 * @param dy increment for the y position, positive or negative
 * @param animated When true, will show a nice transition from the current
 * viewport position to the aforementioned area.
 * @return true on success, false on failure.
 */
bool map_gauge_manipulate_viewport(MapGauge *self, int32_t dx, int32_t dy, bool animated)
{
    self->last_manipulation = SDL_GetTicks();
    self->roaming = true;
    return map_gauge_set_viewport(self,
            self->world_x + dx,
            self->world_y + dy,
            animated
    );
}

/**
 * @brief Resets the viewport to show the area surrounding the marker
 * with the marker in the center.
 *
 * Client code can use this function to reset the view on command. Doing
 * so is not mandatory as the default behavior of the gauge is to go back
 * to a marker-on-center position when:
 * -Roaming mode expores, @see map_gauge_manipulate_viewport
 * -The marker reaches the edges of the current viewport
 *
 * @param self a MapGauge
 * @param animated When true, will show a nice transition from the current
 * viewport position to the aforementioned area.
 * @return true on success, false on failure.
 */
bool map_gauge_center_on_marker(MapGauge *self, bool animated)
{
    return map_gauge_set_viewport(self,
            map_gauge_marker_left(self) - base_gauge_center_x(BASE_GAUGE(self)),
            map_gauge_marker_top(self) - base_gauge_center_y(BASE_GAUGE(self)),
            animated
    );
}

/**
 * @brief Move the viewport according to the current marker position
 *
 * Mainly internal function
 *
 * @param self a MapGauge
 * @return true on success, false on failure
 */
bool map_gauge_follow_marker(MapGauge *self)
{
    bool visible;

    visible = SDL_IntersectRect(&map_gauge_viewport(self),
        &map_gauge_marker_worldbox(self),
        &self->state.marker_src
    );
    if(!visible){
        return map_gauge_center_on_marker(self, true);
    }

    /*marker_x and marker_y are top left coordinates (world)*/
    if( map_gauge_marker_left(self) <= self->world_x + PIX_LIMIT
        || map_gauge_marker_left(self) + generic_layer_w(&self->marker.layer) >= self->world_x + base_gauge_w(BASE_GAUGE(self)) - PIX_LIMIT
        || map_gauge_marker_top(self) <= self->world_y + PIX_LIMIT
        || map_gauge_marker_top(self) + generic_layer_h(&self->marker.layer) >= self->world_x + base_gauge_h(BASE_GAUGE(self)) - PIX_LIMIT
    )
        return map_gauge_center_on_marker(self, true);
    return true;
}

/**
 * @brief Moves the viewport by the given increment (in pixels).
 *
 * Mainly for interal use, might not be the function you are looking for.
 * @see map_gauge_manipulate_viewport
 *
 * @param self a MapGauge
 * @param dx increment for the x position, positive or negative
 * @param dy increment for the y position, positive or negative
 * @param animated When true, will show a nice transition from the current
 * viewport position to the aforementioned area.
 * @return true on success, false on failure.
 */
bool map_gauge_move_viewport(MapGauge *self, int32_t dx, int32_t dy, bool animated)
{
    return map_gauge_set_viewport(self,
            self->world_x + dx,
            self->world_y + dy,
            animated
    );
}

/**
 * @brief Sets the viewport to the given position (in pixels). The position
 * is a "world" position in the virtual current map level that goes from
 * 0,0 to 2^level-1,2^level-1.
 *
 * This function takes an absolute position to go to. For a movement relative
 * to the current position, @see map_gauge_move_viewport.
 *
 * Mainly for interal use, might not be the function you are looking for.
 * @see map_gauge_manipulate_viewport
 *
 * @param self a MapGauge
 * @param x The new absolute x position
 * @param y The new absolute y position
 * @param animated When true, will show a nice transition from the current
 * viewport position to the aforementioned area.
 * @return true on success, false on failure.
 */
bool map_gauge_set_viewport(MapGauge *self, uint32_t x, uint32_t y, bool animated)
{
    uint32_t map_lastcoord = map_math_size(self->level) - 1;
    x = clamp(x, 0, map_lastcoord - base_gauge_w(BASE_GAUGE(self)));
    y = clamp(y, 0, map_lastcoord - base_gauge_h(BASE_GAUGE(self)));

    if(x == self->world_x && y == self->world_y)
        return false;

    animated = false;
    if(animated){
    /*start an animation that moves current coords to their
     * destination values*/
    }else{
        self->world_x = x;
        self->world_y = y;
        BASE_GAUGE(self)->dirty = true;
    }
    return true;
}

/*TODO: split up*/
static void map_gauge_update_state(MapGauge *self, Uint32 dt)
{
    /* We go up to level 16, which is 65536 tiles
     * (from 0 to 65535) in both directions*/
    uintf16_t tl_tile_x, tl_tile_y; /*top left*/
    uintf16_t br_tile_x, br_tile_y; /*bottom right*/

    tl_tile_x = self->world_x / TILE_SIZE;
    tl_tile_y = self->world_y / TILE_SIZE;

    uint32_t lastx = self->world_x + base_gauge_w(BASE_GAUGE(self)) - 1;
    uint32_t lasty = self->world_y + base_gauge_h(BASE_GAUGE(self)) - 1;
    br_tile_x = lastx / TILE_SIZE;
    br_tile_y = lasty / TILE_SIZE;

    uintf16_t x_tile_span = (br_tile_x - tl_tile_x) + 1;
    uintf16_t y_tile_span = (br_tile_y - tl_tile_y) + 1;
    uintf16_t tile_span = x_tile_span * y_tile_span;

    /*There will be as many patches as tiles over which we are located*/
    /*TODO: Multiply by the number of providers*/
    if(tile_span > self->state.apatches){
        void *tmp;
        size_t stmp;
        stmp = self->state.apatches;
        self->state.apatches = tile_span;
        tmp = realloc(self->state.patches, self->state.apatches*sizeof(MapPatch));
        if(!tmp){
            self->state.apatches = stmp;
            return;
        }
        self->state.patches = tmp;
    }

    for(int i = 0; i < self->state.npatches; i++)
        generic_layer_unref(self->state.patches[i].layer);
    self->state.npatches = 0;

    GenericLayer *layer;
    SDL_Rect viewport = map_gauge_viewport(self);
    for(int tiley = tl_tile_y; tiley <= br_tile_y; tiley++){
        for(int tilex = tl_tile_x; tilex <= br_tile_x; tilex++){
            for(int i = 0; i < self->ntile_providers; i++){
                layer = map_tile_provider_get_tile(self->tile_providers[i],
                    self->level,
                    tilex, tiley
                );
                if(layer)
                    break;
            }
            if(!layer)
                printf("Couldn't get tile layer for tile x:%d y:%d zoom:%d\n",tilex,tiley, self->level);
            if(!layer) continue;
            /*TODO: Use rects with uint32_t,
             * SDL uses ints and will only go up to level 15*/
            SDL_Rect tile = {
                .x = TILE_SIZE * tilex,
                .y = TILE_SIZE * tiley,
                .w = TILE_SIZE,
                .h = TILE_SIZE
            };
            /*Get intersection of the tile with the viewport, in world coordinates*/
            SDL_IntersectRect(&viewport,
                &tile,
                &self->state.patches[self->state.npatches].src
            );
            self->state.patches[self->state.npatches].dst = self->state.patches[self->state.npatches].src;
            /*Change src to be in the tile's local coordinates (0-255)*/
            self->state.patches[self->state.npatches].src.x -= tile.x;
            self->state.patches[self->state.npatches].src.y -= tile.y;
            /*Change dst to be in the viewport's local coordinates (0-(w-1),0-(h-1)*/
            self->state.patches[self->state.npatches].dst.x -= self->world_x;
            self->state.patches[self->state.npatches].dst.y -= self->world_y;

            self->state.patches[self->state.npatches].layer = layer;
            generic_layer_ref(layer);
            self->state.npatches++;
        }
    }

    /*Get intersection of the marker with the viewport, in world coordinates*/
    bool marker_visible = SDL_IntersectRect(&viewport,
        &map_gauge_marker_worldbox(self),
        &self->state.marker_src
    );
    if(marker_visible){
        self->state.marker_dst = self->state.marker_src;
        /*Change src to be in the marker's local coordinates (0-(w-1),0-(h-1)*/
        self->state.marker_src.x -= map_gauge_marker_left(self);
        self->state.marker_src.y -= map_gauge_marker_top(self);
        /*Change dst to be in the viewport's local coordinates (0-(w-1),0-(h-1)*/
        self->state.marker_dst.x -= self->world_x;
        self->state.marker_dst.y -= self->world_y;
    }else{
        self->state.marker_dst = (SDL_Rect){-1,-1,-1,-1};
        self->state.marker_src = self->state.marker_dst;
    }
}

static void map_gauge_render(MapGauge *self, Uint32 dt, RenderContext *ctx)
{
    MapPatch *patch;


    for(int i = 0; i < self->state.npatches; i++){
        patch = &self->state.patches[i];
        base_gauge_blit_layer(BASE_GAUGE(self), ctx,
            patch->layer, &patch->src,
            &patch->dst
        );
    }
    if(self->state.marker_src.x >= 0){
#if 0
        base_gauge_blit_layer(BASE_GAUGE(self), ctx,
            &self->marker,
            &self->state.marker_src,
            &self->state.marker_dst
        );
#endif
        base_gauge_blit_rotated_texture(BASE_GAUGE(self), ctx,
            self->marker.layer.texture, &self->state.marker_src,
            self->marker.heading,
            NULL,
            &self->state.marker_dst,
            NULL);
    }
    base_gauge_draw_outline(BASE_GAUGE(self), ctx, &SDL_WHITE, NULL);
}
