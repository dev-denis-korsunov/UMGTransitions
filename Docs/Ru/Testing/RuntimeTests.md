# Runtime и editor-тесты

Эти тесты имеют `EngineFilter`: они проверяют поведение и инварианты, а не скорость. Их задача — сделать безопасными изменения структуры `FWidgetTransition`, spring storage, binding и Blueprint-метаданных.

| Тест | Что проверяется | Почему это важно |
| --- | --- | --- |
| `Diagnostics.StorageLayout` | Печатает `sizeof`/`alignof` ключевых типов и расчётный объём памяти для 100 и 500 linear/spring transition. | Это диагностический бюджет, а не ложный pass/fail лимит: изменение layout становится видимым в журнале до профилирования. |
| `WidgetSelector.Runtime.Hierarchy` | DFS-порядок, уровни, имена, parents и переход через вложенный `WidgetTree → UUserWidget`. | Защищает selector от обрыва поиска на границе составного UserWidget. |
| `WidgetSelector.Diagnostics.GridWaveTranslation` | Создаёт детерминированную сетку 5×5 одинаковых `UImage`-слотов и проверяет `RenderTransform.Translation = WaveDirection * 40`: центр не смещается, остальные ячейки имеют одинаковую длину смещения. | Отделяет математику направления волны от Delay и проверяет, что результат перехода не зависит от порядка обхода. |
| `WidgetSelector.Diagnostics.NoDuplicateDescendants` | Проверяет, что повторное добавление виджета в панель не создаёт повторную запись, а результат `GetWidgetDescendants` содержит каждый указатель ровно один раз. | Защищает оркестрацию от двойного запуска анимации одного виджета. |
| `Runtime.SpecBuilders` | Pure-функции `Create`, `From`, `Options`, `Repeat`, `YoYo` и `Spring` сохраняют независимые параметры, типы значений, binding и callback update interval. | Blueprint pure-цепочка строит value-semantics объект; тест не даёт одному modifier потерять данные другого. |
| `Runtime.PropertyBinding.Channels` | Fast binding resolve/read/write для `RenderOpacity`, всех поддержанных полей `RenderTransform` и pivot; проверяет число каналов. | Защищает публичные getter/setter пути UE и соответствие между типом Widget Property и числом каналов transition value. |
| `Runtime.Spring.Converges` | Единый четырёхканальный spring достигает target. | После оптимизаций частоты, damping и completion check пружина обязана сохранять корректную сходимость для float, vector и color каналов. |
| `Runtime.UpdateInterval` | `Updated` и FieldNotify с интервалом 0.033 секунды вызываются по накопленному времени; финальное значение отправляется, даже если интервал ещё не достигнут. | Позволяет уменьшать стоимость Blueprint callback и push-binding без зависимости от FPS и без пропуска конечного состояния transition. |
| `Runtime.RemoveAtSwap` | Удаление transition с spring перемещает последний transition и spring без рассинхронизации `SpringIndex` и `SpringTransitionIndices`. | `TArray<FWidgetTransition>` использует нестабильные индексы. Это главный структурный инвариант архитектуры с dense spring pass. |
| `Editor.Metadata` | `CreateWidgetTransition` сохраняет metadata `UMGTransitionsBinding`. | Кастомный pin Widget Property в Blueprint строится на этой мета-информации. Без теста рефакторинг UFUNCTION может тихо сломать редакторский UX. |

## Что не покрывают runtime-тесты

- Они не измеряют Blueprint UI, Slate layout и интерактивность editor pin widgets.
- Они не проверяют произвольный `FDynamicPropertyPath` и material parameter на всех типах widget — это остаётся зоной integration-тестов проекта.
- Они не устанавливают performance-бюджет: для этого существуют отдельные perf-тесты.

При добавлении нового fast adapter необходимы минимум две проверки: корректность `Resolve`/`Read`/`Apply` в `Runtime.PropertyBinding.Channels` и отдельная строка в `Performance.FastBindings`.
