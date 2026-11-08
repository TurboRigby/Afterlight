// SPDX-License-Identifier: MPL-2.0
bool water_clip(float origin, float direction, float low, float high, inout float first, inout float last) {
    if (abs(direction) < 1e-8)
        return origin >= low && origin <= high;
    float a = (low - origin) / direction, b = (high - origin) / direction;
    first = max(first, min(a, b));
    last = min(last, max(a, b));
    return first <= last;
}
vec3 underwater_color(vec3 color, vec3 position) {
    if (frame.water_absorption.w <= 0)
        return color;
    vec3 delta = position - frame.camera_position.xyz;
    float c = frame.water_axes.x, s = frame.water_axes.y;
    vec2 o = frame.camera_position.xz - frame.water_bounds.xy;
    vec3 origin = vec3(c * o.x - s * o.y, frame.camera_position.y, s * o.x + c * o.y),
         direction = vec3(c * delta.x - s * delta.z, delta.y, s * delta.x + c * delta.z);
    float first = 0, last = 1;
    if (!water_clip(origin.x, direction.x, -frame.water_bounds.z, frame.water_bounds.z, first, last) ||
        !water_clip(origin.z, direction.z, -frame.water_bounds.w, frame.water_bounds.w, first, last) ||
        !water_clip(origin.y, direction.y, frame.water_axes.z, frame.water_scattering.w + .1, first, last))
        return color;
    float distance = min(100.0, length(delta) * max(0, last - first));
    vec3 transmittance = exp(-frame.water_absorption.rgb * distance);
    return mix(color, color * transmittance + frame.water_scattering.rgb * (vec3(1) - transmittance),
               frame.water_absorption.w);
}
