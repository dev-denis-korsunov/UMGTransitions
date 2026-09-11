# Composer

## Содержание

- [Назначение](#назначение)
- [Выбор дерева](#выбор-дерева)
- [Порядок и волны](#порядок-и-волны)
- [Производительность и тестирование](#производительность-и-тестирование)
  - [Стоимость и применение](#стоимость-и-применение)



## Назначение

`UWidgetSelectorLibrary` — Blueprint library для получения виджетов из иерархии UMG. Composer связывает выбор группы и её хореографию: сначала выбираются дети, затем их `WaveIndex` используется для delay или variation transition.

Traversal пересекает границу `WidgetTree → UUserWidget`, поэтому composed User Widget ведёт себя как продолжение общей иерархии.

## Выбор дерева

| Задача | Функция |
| --- | --- |
| Получить корень User Widget | `Get Widget Tree Root` |
| Найти именованный widget | `Find Widget by Name` |
| Получить прямых детей | `Get Widget Children` |
| Получить descendants | `Get Widget Descendants` |
| Ограничить уровень | `Get Widgets at Depth` / `Get Widgets through Depth` |
| Подняться к родителям | `Get Widget Parent` / `Get Widget Parents` |
| Выбрать descendants по именам | `Find Widget Descendants by Name(s)` |

`Get Widget Descendants` возвращает `FWidgetDescendant`: widget, depth, one-based wave index и нормализованное направление wave.

## Порядок и волны

Traversal может быть Depth First или Breadth First. Для sibling order доступны Left to Right, Right to Left и Center Out. Wave рассчитывается по geometry top-level children; deeper descendants наследуют wave родителя.

Wave patterns:

- Horizontal;
- Vertical;
- Manhattan;
- Radial.

Origin выбирается как угол, центр или конкретный widget. Для staggered enter-animation обычно достаточно `Delay = (WaveIndex - 1) × Offset`, чтобы первая волна стартовала без дополнительного ожидания.


## Производительность и тестирование

| Тест | Нагрузка | Проверка | Граница покрытия |
| --- | --- | --- | --- |
| Runtime.Hierarchy | Root + 5 descendants, включая nested UserWidget | DFS/BFS, порядок, depth, names, parents, wave inheritance | Не большой UI |
| Diagnostics.GridWaveTranslation | Сетка 5×5, 25 Image, 40 px, 4 tick по 0.05 с | Записанные vector endpoints и достижение translation | Направления заданы в тесте; cached-geometry wave не проверяется |
| Diagnostics.NoDuplicateDescendants | 3 descendants, повторное AddChild одного Widget | Уникальность и сохранение вложенного ребёнка | Один маленький пример владения |

Имена указаны относительно `UMGTransitions.WidgetSelector`. В историческом журнале есть Passed для Hierarchy (24.08.2026, UE 5.7 / Mac arm64 Development); отдельные результаты новых diagnostics в нём не записаны. Проверка исходника подтверждает наличие теста, но не его успешный запуск.

### Стоимость и применение

Отдельных замеров времени selection на 100/500/1000 widgets нет. A/B выборки по geometry отсутствует; сравнивать DFS/BFS по скорости на этих данных нельзя.

**Вывод:** имеющееся покрытие защищает базовый порядок и владение. Вычисляйте selection при изменении состава группы, сохраняйте результат для одного запуска хореографии; это избегает повторного обхода pure-функции при нескольких потребителях Blueprint. Geometry-wave требует актуального layout. Это рекомендация из устройства кода, не измеренный процент ускорения. [Методика и пробелы](Testing.md#методика-сравнений).
