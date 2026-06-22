from dataclasses import dataclass
import math


RETRACT_MM = 30.0
MAX_SEGMENTS = 100_000
EPSILON = 1e-9


class ToolpathValidationError(ValueError):
    pass


@dataclass(frozen=True)
class Point3D:
    x: float
    y: float
    z: float


@dataclass(frozen=True)
class ToolpathSegment:
    start: Point3D
    end: Point3D
    feed_mm_s: float
    kind: str
    level: int


@dataclass(frozen=True)
class ToolpathResult:
    segments: tuple
    levels_z: tuple
    passes_per_level: int
    total_passes: int
    initial_point: Point3D
    final_cut_point: Point3D
    final_point: Point3D
    stepover_mm: float

    @property
    def segment_count(self):
        return len(self.segments)


def _positive_finite(name, value):
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        raise ToolpathValidationError(f"{name}: valore non numerico") from None
    if not math.isfinite(parsed) or parsed <= 0.0:
        raise ToolpathValidationError(f"{name}: deve essere maggiore di zero")
    return parsed


def _finite_coordinate(name, value):
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        raise ToolpathValidationError(f"{name}: coordinata non numerica") from None
    if not math.isfinite(parsed):
        raise ToolpathValidationError(f"{name}: coordinata non valida")
    return parsed


def _inclusive_positions(minimum, maximum, step):
    positions = [minimum]
    while positions[-1] + step < maximum - EPSILON:
        positions.append(positions[-1] + step)
        if len(positions) > MAX_SEGMENTS:
            raise ToolpathValidationError("Numero di passate troppo elevato")
    if not math.isclose(positions[-1], maximum, rel_tol=0.0, abs_tol=EPSILON):
        positions.append(maximum)
    return positions


def _depth_levels(total_depth, depth_per_pass):
    level_count = max(1, int(math.ceil((total_depth / depth_per_pass) - EPSILON)))
    if level_count > MAX_SEGMENTS:
        raise ToolpathValidationError("Numero di livelli Z troppo elevato")
    return [min(index * depth_per_pass, total_depth) for index in range(1, level_count + 1)]


def generate_spianatura_xy(
    *,
    start_x,
    start_y,
    end_x,
    end_y,
    total_depth,
    depth_per_pass,
    tool_diameter,
    overlap,
    feed_xy,
    plunge_feed_z,
    retract_mm=RETRACT_MM,
):
    start_x = _finite_coordinate("X inizio", start_x)
    start_y = _finite_coordinate("Y inizio", start_y)
    end_x = _finite_coordinate("X fine", end_x)
    end_y = _finite_coordinate("Y fine", end_y)
    total_depth = _positive_finite("Profondità totale", total_depth)
    depth_per_pass = _positive_finite("Profondità di passata", depth_per_pass)
    tool_diameter = _positive_finite("Diametro utensile", tool_diameter)
    feed_xy = _positive_finite("Velocità avanzamento XY", feed_xy)
    plunge_feed_z = _positive_finite("Velocità affondo Z", plunge_feed_z)
    retract_mm = _positive_finite("Risalita finale", retract_mm)

    try:
        overlap = float(overlap)
    except (TypeError, ValueError):
        raise ToolpathValidationError("Sovrapposizione: valore non numerico") from None
    if not math.isfinite(overlap) or overlap < 0.0:
        raise ToolpathValidationError("Sovrapposizione: non può essere negativa")
    if overlap >= tool_diameter:
        raise ToolpathValidationError("Sovrapposizione: deve essere minore del diametro utensile")

    x_min, x_max = sorted((start_x, end_x))
    y_min, y_max = sorted((start_y, end_y))
    if math.isclose(x_min, x_max, rel_tol=0.0, abs_tol=EPSILON):
        raise ToolpathValidationError("Area non valida: inizio e fine X coincidono")
    if math.isclose(y_min, y_max, rel_tol=0.0, abs_tol=EPSILON):
        raise ToolpathValidationError("Area non valida: inizio e fine Y coincidono")

    stepover = tool_diameter - overlap
    pass_positions = _inclusive_positions(y_min, y_max, stepover)
    levels = _depth_levels(total_depth, depth_per_pass)
    expected_segments = len(levels) * (2 * len(pass_positions)) + 1
    if expected_segments > MAX_SEGMENTS:
        raise ToolpathValidationError(
            f"Percorso troppo grande: {expected_segments} segmenti (massimo {MAX_SEGMENTS})"
        )

    current = Point3D(x_min, y_min, 0.0)
    initial_point = current
    segments = []
    current_x_is_min = True

    for level_index, depth in enumerate(levels, start=1):
        plunge_target = Point3D(current.x, current.y, depth)
        segments.append(
            ToolpathSegment(current, plunge_target, plunge_feed_z, "PLUNGE", level_index)
        )
        current = plunge_target

        level_passes = pass_positions if level_index % 2 == 1 else list(reversed(pass_positions))
        for pass_index, y_position in enumerate(level_passes):
            target_x = x_max if current_x_is_min else x_min
            cut_target = Point3D(target_x, y_position, depth)
            segments.append(ToolpathSegment(current, cut_target, feed_xy, "CUT", level_index))
            current = cut_target
            current_x_is_min = not current_x_is_min

            if pass_index < len(level_passes) - 1:
                step_target = Point3D(current.x, level_passes[pass_index + 1], depth)
                segments.append(ToolpathSegment(current, step_target, feed_xy, "STEP_OVER", level_index))
                current = step_target

    final_cut_point = current
    final_point = Point3D(current.x, current.y, current.z - retract_mm)
    segments.append(ToolpathSegment(current, final_point, plunge_feed_z, "RETRACT", len(levels)))

    return ToolpathResult(
        segments=tuple(segments),
        levels_z=tuple(levels),
        passes_per_level=len(pass_positions),
        total_passes=len(pass_positions) * len(levels),
        initial_point=initial_point,
        final_cut_point=final_cut_point,
        final_point=final_point,
        stepover_mm=stepover,
    )
