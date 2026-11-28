INSERT INTO wdt_territory (id, name, map_name, owner, sort_order) VALUES
    ('northern-pass', 'Northern Pass', 'WDTRIDGE.MPR', 'Neutral', 4),
    ('southern-flats', 'Southern Flats', 'WDTBASIN.MPR', 'Neutral', 5)
ON DUPLICATE KEY UPDATE id = VALUES(id);
