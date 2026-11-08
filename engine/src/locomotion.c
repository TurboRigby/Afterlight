/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/locomotion.h>
#include <math.h>
af_locomotion af_locomotion_default(void) {
    return (af_locomotion){.walk_speed = 2, .run_speed = 5, .transition = 1};
}
const char *af_locomotion_state_name(af_locomotion_state s) {
    static const char *names[] = {"Idle", "Walk", "Run", "Jump", "Fall"};
    return s >= AF_LOCOMOTION_IDLE && s <= AF_LOCOMOTION_FALL ? names[s] : "Unknown";
}
bool af_locomotion_update(af_locomotion *m, af_locomotion_input in, float dt) {
    if (!m || !isfinite(dt) || dt <= 0 || dt > .1f || !isfinite(in.speed) || in.speed < 0 || in.speed > 100 ||
        !isfinite(in.vertical_speed) || fabsf(in.vertical_speed) > 1000 || !isfinite(m->walk_speed) ||
        !isfinite(m->run_speed) || m->walk_speed <= 0 || m->run_speed <= m->walk_speed)
        return false;
    af_locomotion_state next;
    if (!in.grounded)
        next = in.vertical_speed > .1f ? AF_LOCOMOTION_JUMP : AF_LOCOMOTION_FALL;
    else if (in.speed < (m->state == AF_LOCOMOTION_IDLE ? .12f : .06f))
        next = AF_LOCOMOTION_IDLE;
    else {
        float threshold = (m->walk_speed + m->run_speed) * .5f + (m->state == AF_LOCOMOTION_RUN ? -.15f : .15f);
        next = in.speed >= threshold ? AF_LOCOMOTION_RUN : AF_LOCOMOTION_WALK;
    }
    bool was_grounded = m->state <= AF_LOCOMOTION_RUN;
    m->landed = m->initialized && !was_grounded && in.grounded;
    m->took_off = m->initialized && was_grounded && !in.grounded;
    if (!m->initialized || next != m->state) {
        m->previous = m->state;
        m->state = next;
        m->time = 0;
        m->transition = 0;
    } else
        m->time += dt;
    m->transition = fminf(1, m->transition + dt / .12f);
    m->speed = in.speed;
    m->initialized = true;
    return true;
}
