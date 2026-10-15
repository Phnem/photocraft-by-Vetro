// Russian for the strings only PhotoCraft by Vetro has (the rest comes from the egui shell's
// ru.tsv through the bridge). Same rows as ru.tsv: context <TAB> English <TAB> Russian; included
// by i18n.cpp as one string (UTF-8: the shell builds with /utf-8). Clean-room, in the ordinary
// Russian graphics-editor vocabulary. Kept in pieces: MSVC limits one string literal's length.
R"TSV(
	Add layer mask	Добавить маску слоя
	Add to Selection	Добавить к выделению
	All files	Все файлы
	Alt-click to choose where to sample from	Alt+щелчок — выбрать, откуда брать образец
	Apply crop	Кадрировать
	Bloat	Вздутие
	Click adds a point · drag one off the grid to remove it	Щелчок добавляет точку · вытащите точку за сетку, чтобы удалить её
	Click corners, drag curves · Enter ends	Щёлкайте углы, тяните кривые · Enter завершает
	Click points · Enter ends	Щёлкайте точки · Enter завершает
	Close Path	Замкнуть контур
	Color Sampler	Цветовой эталон
	Create Shape Layer	Создать слой-фигуру
	Create Vector Mask	Создать векторную маску
	Curvature Pen	Перо кривизны
	Delete Anchor Point	Удалить опорную точку
	Delete Last Anchor Point	Удалить последнюю опорную точку
	Diamond	Ромбовидный
	Direct Selection	Стрелка
	Direct3D 11 / Direct2D could not start.	Не удалось запустить Direct3D 11 / Direct2D.
	Drag points and handles · Delete removes	Тяните точки и ручки · Delete удаляет
	Drag the path · Delete removes	Тяните контур · Delete удаляет
	Draw a frame, then fit its corners	Нарисуйте рамку и подгоните её углы
	Draw the path	Нарисуйте контур
	Ellipse	Эллипс
	Elliptical Marquee	Овальная область
	End Open Path	Завершить открытый контур
	Enter to keep  ·  Esc to cancel	Enter — сохранить  ·  Esc — отмена
	Enter to place  ·  Esc to cancel	Enter — разместить  ·  Esc — отмена
	Fit on screen  (Ctrl+0)	Показать целиком  (Ctrl+0)
	Foreground / background  (X swaps, D resets)	Основной / фоновый  (X меняет местами, D сбрасывает)
	Forward Warp	Деформация
	Free Transform  ·  Shift: free ratio  ·  Alt: from centre  ·  Ctrl: distort	Свободное трансформирование  ·  Shift — без пропорций  ·  Alt — от центра  ·  Ctrl — искажение
	Freeform Pen	Свободное перо
	Freeze Mask	Заморозить
	Grading	Цветокоррекция
	Horizontal Type	Горизонтальный текст
	Images	Изображения
	In %d   Out %d	Вход %d   Выход %d
	Intersect	Пересечение
	Layer style	Стиль слоя
	Liquify	Пластичность
	Liquify  ·  paint on the photo  ·  Enter applies, Esc cancels	Пластичность  ·  рисуйте по фото  ·  Enter применяет, Esc отменяет
	Magic Eraser	Волшебный ластик
	Magic Wand	Волшебная палочка
	Maximise	Развернуть
	Minimise	Свернуть
	Move the corners onto the edges · Enter crops	Совместите углы с краями · Enter кадрирует
	New adjustment layer	Новый корректирующий слой
	New group	Новая группа
	New layer	Новый слой
	New note	Новая заметка
	New text layer	Новый текстовый слой
	No layer	Нет слоя
	No layers yet	Слоёв пока нет
	No document	Нет документа
	Note	Заметка
	Nothing to set for this effect here.	Для этого эффекта здесь нечего настраивать.
	Nothing to transform on this layer	На этом слое нечего трансформировать
	Object Selection	Выделение объектов
	Open a photo to begin	Откройте фото, чтобы начать
	Open documents	Открытые документы
	Close document	Закрыть документ
	Close others	Закрыть другие
	Reveal in Explorer	Показать в проводнике
	Recent documents	Последние документы
	No recent documents	Нет недавних документов
	Clear recent documents	Очистить список недавних документов
	Opened files will appear here	Здесь появятся открытые файлы
	Create a document or open an image	Создайте документ или откройте изображение
	New document…	Создать документ…
	Save changes to %ls before closing?	Сохранить изменения в «%ls» перед закрытием?
	Recent documents could not be saved.	Не удалось сохранить список недавних документов.
	The clipboard is busy. Try again.	Буфер обмена занят. Попробуйте ещё раз.
	The clipboard image could not be read.	Не удалось прочитать изображение из буфера обмена.
	The clipboard image could not be encoded.	Не удалось подготовить изображение для буфера обмена.
	The clipboard image is too large.	Изображение в буфере обмена слишком большое.
	The clipboard could not be updated.	Не удалось обновить буфер обмена.
	Reading clipboard	Чтение буфера обмена
)TSV"
R"TSV(
	Layer	Слой
	Background	Фон
	Group	Группа
	Shape	Фигура
	copy	копия
	Auto-Select	Автовыбор
	Show Transform Controls	Показать управляющие элементы
	No layers here	Здесь нет слоёв
	Expand or collapse group	Развернуть или свернуть группу
	Rotate View	Поворот вида
	Rotation	Поворот
	Reset View	Сбросить поворот
	W %.0f px  ×  H %.0f px	Ш %.0f пикс.  ×  В %.0f пикс.
	Use global light	Глобальное освещение
	Anti alias	Сглаживание
	Knocks out	Слой выбивает тень
	Spread	Размах
	Distance	Смещение
	Direction	Направление
	Up	Вверх
	Down	Вниз
	Shallow	Неглубокое
	Deep	Глубокое
	Smoothness	Плавность
	Color burn	Затемнение основы
	Color dodge	Осветление основы
	Darker color	Темнее
	Lighter color	Светлее
	Hard light	Жёсткий свет
	Hard mix	Жёсткое смешение
	Linear burn	Линейный затемнитель
	Linear dodge	Линейный осветлитель
	Linear light	Линейный свет
	Pin light	Точечный свет
	Vivid light	Яркий свет
	Choke	Стягивание
	Technique	Метод
	Softer	Мягкий
	Precise	Точный
	Edge	На краях
	Center	Из центра
	Outside	Снаружи
	Inside	Внутри
	Inner	Внутренний
	Outer	Внешний
	Emboss	Тиснение
	Pillow	Подушечное тиснение
	Chisel hard	Жёсткая огранка
	Chisel soft	Мягкая огранка
	Soften	Смягчение
	Altitude	Высота
	Contour name	Контур
	Contour range	Диапазон контура
	Contour anti alias	Сглаживание контура
	Gloss contour	Контур глянца
	Texture scale	Масштаб текстуры
	Texture depth	Глубина текстуры
	Texture invert	Инвертировать текстуру
	Texture link	Связать текстуру со слоем
	Fill opacity	Непрозрачность заливки
	Blend interior effects as group	Внутренние эффекты как группа
	Blend clipped layers as group	Обтравочные слои как группа
	Transparency shapes layer	Слой-фигура прозрачности
	Layer mask hides effects	Маска слоя скрывает эффекты
	Vector mask hides effects	Векторная маска скрывает эффекты
	Phase x	Смещение X
	Phase y	Смещение Y
	From	Начальный цвет
	To	Конечный цвет
	Link	Связать со слоем
	Classic	Классический
	Perceptual	Перцепционный
	Cone	Конус
	Cone - inverted	Перевёрнутый конус
	Cove - deep	Глубокая выемка
	Cove - shallow	Мелкая выемка
	Gaussian	Гауссовский
	Half round	Полукруг
	Ring	Кольцо
	Ring - double	Двойное кольцо
	Rolling slope - descending	Нисходящий склон
	Rounded steps	Скруглённые ступени
	Sawtooth 1	Пилообразный 1
)TSV"
R"TSV(
	Paint Bucket	Заливка
	Path Selection	Выделение контура
	PhotoCraft by Vetro  ·  drop a photo here or press Ctrl+O	PhotoCraft by Vetro  ·  перетащите фото сюда или нажмите Ctrl+O
	Polygon	Многоугольник
	Polygonal Lasso	Прямолинейное лассо
	Pucker	Сморщивание
	Push Left	Смещение пикселов
	Radial	Радиальный
	Rectangular Marquee	Прямоугольная область
	Reflected	Зеркальный
	Rounded Rectangle	Прямоугольник со скруглёнными углами
	Triangle	Треугольник
	Select Inverse	Инверсия выделения
	Show or hide	Показать или скрыть
	Single Column Marquee	Область (вертикальная строка)
	Single Row Marquee	Область (горизонтальная строка)
	Some documents have unsaved changes. Quit anyway?	В некоторых документах есть несохранённые изменения. Всё равно выйти?
	Source set	Источник задан
	Star	Звезда
	Subtract from Selection	Вычесть из выделения
	Thaw Mask	Разморозить
	The PhotoCraft engine could not start.	Не удалось запустить движок PhotoCraft.
	The frame has to stay convex	Рамка должна оставаться выпуклой
	This command has no settings.	У этой команды нет настроек.
	Twirl Clockwise	Скручивание по часовой
	Twirl Counterclockwise	Скручивание против часовой
	Type, then press Enter	Введите текст и нажмите Enter
	Vertical Type	Вертикальный текст
	Warp  ·  drag the points, or bend the grid from inside	Деформация  ·  тяните точки или изгибайте сетку изнутри
	Working	Работаю
	Zoom in  (Ctrl+=)	Увеличить  (Ctrl+=)
	Zoom out  (Ctrl+-)	Уменьшить  (Ctrl+-)
	Language	Язык
	Merge Down	Объединить с предыдущим
	Merge Visible	Объединить видимые
	Rename Layer	Переименовать слой
	Delete Layer	Удалить слой
	Release Clipping Mask	Отменить обтравочную маску
	Rasterize Layer Style	Растрировать стиль слоя
	Pixel layer	Пиксельный слой
	Adjustment layer	Корректирующий слой
	Text layer	Текстовый слой
	Shape layer	Слой-фигура
	Group layer	Группа слоёв
	Fill layer	Слой-заливка
	Smart layer	Смарт-объект
	SmartObject layer	Смарт-объект
	Artboard layer	Монтажная область
	Image layer	Слой изображения
	Smoothing	Сглаживание
	Radius	Радиус
	Opened %ls	Открыто: %ls
	Opening %ls	Открывается: %ls
	Saved %ls	Сохранено: %ls
	Saving %ls	Сохраняется: %ls
	%ls is not in this shell yet	%ls: пока нет в этой оболочке
	%ls… one moment	%ls… секунду
	Ruler: %.1f px at %.1f°	Линейка: %.1f пикс., угол %.1f°
	%.0f%%   ·   %ls %d-bit   ·   %d × %d px   ·   %d layers   ·   %ls%ls	%.0f%%   ·   %ls %d бит   ·   %d × %d пикс.   ·   слоёв: %d   ·   %ls%ls
	 · monitor profile	 · профиль монитора
	%.0f px	%.0f пикс.
	 px	 пикс.
	 pt	 пт
	%.0f px  ·  %.0f%%	%.0f пикс.  ·  %.0f%%
)TSV"

// Milestone 3: tool parameters and transform/crop/gradient interactions.
R"TSV(
	Align Bottom Edges	По нижним краям
	Align Horizontal Centers	По центру по горизонтали
	Align Left Edges	По левым краям
	Align Right Edges	По правым краям
	Align Top Edges	По верхним краям
	Align Vertical Centers	По центру по вертикали
	Distribute Horizontally	Распределить по горизонтали
	Distribute Vertically	Распределить по вертикали
	All Layers	Все слои
	All without Adjustments	Все слои без коррекции
	Angle	Угол
	Apply	Применить
	Arc	Дуга
	Arc Lower	Нижняя дуга
	Arc Upper	Верхняя дуга
	Arch	Арка
	Auto	Авто
	Bend	Изгиб
	Bicubic	Бикубическая
	Bilinear	Билинейная
	Bulge	Выпуклость
	Cancel	Отмена
	Canvas	Холст
	Classic Gradient	Классический градиент
	Color	Цвет
	Combine	Объединить
	Content-Aware	С учётом содержимого
	Current Layer	Текущий слой
	Current and Below	Текущий и нижние
	Current and Below without Adjustments	Текущий и нижние без коррекции
	Custom	Заказная
	Custom Ratio	Свои пропорции
	Dashed	Пунктир
	Default	По умолчанию
	Delete Cropped Pixels	Удалить отсечённые пиксели
	Delete Stop	Удалить точку
	Diagonal	Диагонали
	Distort	Искажение
	Dither	Дизеринг
	Dotted	Точки
	Edit Gradient…	Редактировать градиент…
	Exclude	Исключить
	Fill	Заливка
	Fish	Рыба
	Fisheye	Рыбий глаз
	Flag	Флаг
	Flip Horizontal	Отразить по горизонтали
	Flip Vertical	Отразить по вертикали
	Free Transform	Свободное трансформирование
	Golden Ratio	Золотое сечение
	Golden Spiral	Золотая спираль
	Gradient Editor	Редактор градиента
	Grid	Сетка
	H	В
	Horizontal	По горизонтали
	Inflate	Надуть
	Linear	Линейный
	Location	Позиция
	Nearest	По соседним пикселям
	New Layer	Новый слой
	Opacity	Непрозрачность
	Original Ratio	Исходные пропорции
	Output H	Высота на выходе
	Output W	Ширина на выходе
	Perspective	Перспектива
	Point Sample	Точечный образец
	Presets…	Наборы…
	Ratio H	Пропорции В
	Ratio W	Пропорции Ш
	Reference point	Опорная точка
	Reset	Сбросить
	Resolution	Разрешение
	Reverse	Инверсия
	Rise	Подъём
	Rotate	Поворот
	Rotate 180°	Повернуть на 180°
	Rotate 90° Clockwise	Повернуть на 90° по часовой
	Rotate 90° Counterclockwise	Повернуть на 90° против часовой
	Rule of Thirds	Правило третей
	Scale	Масштаб
	Selection	Выделение
	Set both output width and height	Задайте ширину и высоту на выходе
	Shell Lower	Нижняя раковина
	Shell Upper	Верхняя раковина
	Show Sampling Ring	Кольцо сравнения
	Skew	Наклон
	Solid	Сплошная
	Split crosswise	Разбить крестом
	Split horizontally	Разбить по горизонтали
	Split vertically	Разбить по вертикали
	Squeeze	Сжатие
	Straighten	Выпрямить
	Stroke	Обводка
	Stroke Width	Толщина обводки
	Subtract	Вычесть
	Twist	Скручивание
	Unconstrained	Без ограничений
	Vertical	По вертикали
	Vertical warp	Вертикальная деформация
	W	Ш
	Warp	Деформация
	Warp grid is too large	Слишком большая сетка деформации
	Wave	Волна
	Anti-alias	Сглаживание
	Sample All Layers	Образец со всех слоёв
	Select Subject	Выделить предмет
	Select and Mask…	Выделение и маска…
	Save Selection…	Сохранить выделение…
	Load Selection…	Загрузить выделение…
	Save Selection	Сохранить выделение
	New selection channel…	Новый канал выделения…
	Load inverted	Загрузить с инверсией
	Composite	Составной канал
	Layer Transparency	Прозрачность слоя
	Layer Mask	Маска слоя
	Vector Mask	Векторная маска
	Replace	Заменить
	Add	Добавить
	Make Work Path…	Создать рабочий контур…
	Transform Selection	Трансформировать выделение
	Last Filter	Последний фильтр
	Fade…	Ослабить…
	%.1f × %.1f px  ·  %.1f°	%.1f × %.1f пикс.  ·  %.1f°
)TSV"

R"TSV(
	Link proportions	Пропорции
	Stroke Center	По центру
	Distribute Top Edges	Распределить по верхним краям
	Distribute Vertical Centers	Распределить по центрам по вертикали
	Distribute Bottom Edges	Распределить по нижним краям
	Distribute Left Edges	Распределить по левым краям
	Distribute Horizontal Centers	Распределить по центрам по горизонтали
	Distribute Right Edges	Распределить по правым краям
	Gradient Fill	Градиент
	With Fill	С заливкой
	With Stroke	С обводкой
)TSV"

R"TSV(
	Content-Aware Crop Fill	Заполнение краёв
	Content-Aware Crop	Кадрирование с учётом содержимого
)TSV"

R"TSV(
	Choose font…	Выбрать шрифт…
	Search fonts	Поиск шрифтов
	Type layer	Текстовый слой
	Layer Filters	Фильтры слоёв
	All groups	Все группы
	All types	Все типы
	All colors	Все метки
	Import ABR…	Импорт ABR…
	With effects	С эффектами
	Clear filters	Сбросить фильтры
	Locks	Замки
	Color Label	Цветовая метка
	Faux bold	Псевдополужирный
	Faux italic	Псевдокурсив
	Horizontal scale	Масштаб по горизонтали
	Vertical scale	Масштаб по вертикали
	Baseline shift	Смещение базовой линии
	Ligatures	Лигатуры
	Discretionary ligatures	Дискреционные лигатуры
	Metrics	Метрический
	Optical	Оптический
	Auto Leading	Автоинтерлиньяж
	Kern	Кернинг
	Select a region, then drag it	Выделите область и перетащите её
	Text is too long	Текст слишком длинный
	Clipboard is unavailable	Буфер обмена недоступен
	Expand effects	Раскрыть эффекты
	Move up	Выше
	Move down	Ниже
	Show Overlay	Показать наложение
	Overlay Opacity	Непрозрачность наложения
	Auto Hide	Автоскрытие
	Clipped	В пределах кисти
	Brush Tip Shape	Форма отпечатка кисти
	Brush Pose	Положение кисти
	Effect	Эффект
)TSV"

R"TSV(
	All blend modes	Все режимы наложения
)TSV"

R"TSV(
	Enable / Disable Mask	Включить / отключить маску
	Delete Filter Mask	Удалить маску фильтров
	Red / Cyan	Красный / голубой
	Green / Magenta	Зелёный / пурпурный
	Blue / Yellow	Синий / жёлтый
	Constant	Константа
	Black point	Точка чёрного
	Gray point	Точка серого
	White point	Точка белого
	Medium Contrast	Средний контраст
	Strong Contrast	Сильный контраст
)TSV"
R"TSV(
	Search	Поиск
	Untitled	Без имени
	Kerning mode	Режим кернинга
	First line indent	Отступ первой строки
	Start indent	Отступ слева
	End indent	Отступ справа
	Space before	Интервал перед абзацем
	Space after	Интервал после абзаца
	Auto leading	Автоинтерлиньяж
	Justify Center	По ширине, последняя по центру
	Justify Right	По ширине, последняя справа
	Justify All	По ширине все строки
	Hyphenate	Переносить по слогам
	Left to Right	Слева направо
	Right to Left	Справа налево
	Small Caps	Капитель
	All Caps	Все прописные
	Pattern Fill	Заливка узором
	Solid Color	Цветовая заливка
	General	Основные
	Minimum	Минимум
	Fade steps	Шагов затухания
	Pen pressure	Нажим пера
	Pen tilt	Наклон пера
	Stylus wheel	Колесо пера
	Initial direction	Начальное направление
	Build up rate	Интенсивность нарастания
	Tilt scale	Масштаб наклона
	Wet edges	Влажные края
	Dual brush	Двойная кисть
	Color dynamics	Динамика цвета
	Shape dynamics	Динамика формы
	Brush pose	Положение кисти
)TSV"
R"TSV(
	Control	Управление
	Jitter	Колебание
	Hue jitter	Колебание цветового тона
	Saturation jitter	Колебание насыщенности
	Brightness jitter	Колебание яркости
	Depth jitter	Колебание глубины
	Count jitter	Колебание количества
	Fg bg	Основной и фоновый цвета
	Both axes	Обе оси
	Each tip	Каждый отпечаток
	Per tip	Для каждого отпечатка
	Flip x	Отразить по горизонтали
	Flip y	Отразить по вертикали
	Flip x jitter	Случайное отражение по горизонтали
	Flip y jitter	Случайное отражение по вертикали
	Pressure opacity	Непрозрачность от нажима
	Pressure size	Размер от нажима
	Spacing enabled	Использовать интервалы
	Tip	Отпечаток
	Pose	Положение
	Tilt x	Наклон по горизонтали
	Tilt y	Наклон по вертикали
	Override pressure	Переопределить нажим
	Override rotation	Переопределить поворот
	Override tilt	Переопределить наклон
	Brush projection	Проекция кисти
	Protect texture	Защита текстуры
	Procedural	Процедурный
	Aliased	Без сглаживания
	Build up	Нарастание
	Airbrush	Аэрограф
	Adjust for zoom	Учитывать масштаб
	Catch up	Догонять курсор
	Catch up on end	Догонять при завершении
	Pulled string	Натянутая нить
	Mixer	Микс-кисть
	Wetness	Влажность
	Structure	Структура
	Sample all layers	Образец со всех слоёв
	Layer mask	Маска слоя
	Vector mask	Векторная маска
	Smart Filter Blending Options	Параметры наложения смарт-фильтра
	Smart Filter Mask	Маска смарт-фильтров
	bit	бит
	Pixels	Пиксели
	Inches	Дюймы
	Centimeters	Сантиметры
	Millimeters	Миллиметры
	Points	Пункты
	Picas	Пики
	Percent	Проценты
)TSV"
R"TSV(
	Align Left	По левому краю
	Align Center	По центру
	Align Right	По правому краю
	New Guide Layout	Схема направляющих
	New Guide	Новая направляющая
	Column Gutter	Межколонный интервал
	Row Gutter	Межстрочный интервал
	Clear existing guides	Удалить существующие направляющие
)TSV"

R"TSV(
	Outside CMYK gamut	Вне цветового охвата CMYK
	Enter six hexadecimal digits	Введите шесть шестнадцатеричных цифр
	Color Picker	Палитра цветов
	Use printable color	Выбрать печатный цвет
)TSV"
R"TSV(
	New Group…	Новая группа…
	New Swatch	Новый образец
	New Gradient	Новый градиент
	Define Pattern	Определить узор
	Rename Group	Переименовать группу
	Export Group…	Экспортировать группу…
	Move to Group…	Переместить в группу…
	No matching presets	Подходящих наборов нет
	Use Search to narrow the list	Уточните запрос в поиске
	Channel Visibility	Видимость канала
	Duplicate Channel…	Создать копию канала…
	Duplicate Channel	Копия канала
	Delete Channel	Удалить канал
	New Channel…	Новый канал…
	Save Selection as Channel	Сохранить выделение в канал
	New Path…	Новый контур…
	Save / Rename Path…	Сохранить / переименовать контур…
	Save Path	Сохранить контур
	Load Path as Selection	Загрузить контур как выделение
	Foreground…	Основной цвет…
	Background…	Фоновый цвет…
	New Document from State	Новый документ из состояния
	Rename Snapshot…	Переименовать снимок…
)TSV"
R"TSV(
	Snapshot	Снимок
	Color Swatch	Образец цвета
)TSV"
R"TSV(
	Queued actions	В очереди
	Queued control is no longer available	Элемент из очереди больше недоступен
)TSV"

R"TSV(
	Onion Skin	Луковая кожура
	Marching Ants	Марширующие муравьи
	On Black	На чёрном
	On White	На белом
	Smart Radius	Умный радиус
	Shift Edge	Сдвиг края
	Decontaminate Colors	Очистить цвета
	Output To	Вывод
	New Layer with Layer Mask	Новый слой со слоем-маской
	New Document with Layer Mask	Новый документ со слоем-маской
	Create a selection or layer mask first	Сначала создайте выделение или слой-маску
	Select and Mask  ·  refine the edge  ·  Enter applies, Esc cancels	Выделение и маска  ·  уточните край  ·  Enter — применить, Esc — отменить
	Remove Background	Удалить фон
	Select and Mask	Выделение и маска
)TSV"

R"TSV(
	Load Transparency into Mask	Загрузить прозрачность в маску
	Copy Layers to Document	Скопировать слои в документ
	Document is no longer available	Документ больше недоступен
)TSV"

R"TSV(
	Simulate Pressure	Имитировать нажим
	Stroke Path…	Обводка контура…
	Pixels mode needs a pixel layer	Для режима «Пиксели» выберите пиксельный слой
	Pixels mode needs the composite pixel target	Для режима «Пиксели» выберите составной канал
	pixel layer is locked	Пиксельный слой заблокирован
)TSV"
