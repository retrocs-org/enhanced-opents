CREATE TABLE IF NOT EXISTS wdt_territory (
    id VARCHAR(32) PRIMARY KEY,
    name VARCHAR(80) NOT NULL,
    map_name VARCHAR(32) NOT NULL,
    owner ENUM('GDI', 'Nod', 'Neutral') NOT NULL,
    sort_order TINYINT UNSIGNED NOT NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS wdt_battle (
    id CHAR(32) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    territory_id VARCHAR(32) NOT NULL,
    map_name VARCHAR(32) NOT NULL,
    status ENUM('reserved', 'in_progress', 'review', 'won', 'draw', 'aborted', 'expired') NOT NULL,
    winner ENUM('GDI', 'Nod') NULL,
    expires_at BIGINT UNSIGNED NOT NULL,
    started_at BIGINT UNSIGNED NULL,
    finished_at BIGINT UNSIGNED NULL,
    reason VARCHAR(200) NULL,
    active_territory VARCHAR(32) AS (IF(status IN ('reserved', 'in_progress', 'review'), territory_id, NULL)) PERSISTENT,
    UNIQUE KEY one_active_battle (active_territory),
    KEY history (finished_at, id),
    CONSTRAINT wdt_battle_territory FOREIGN KEY (territory_id) REFERENCES wdt_territory(id)
) ENGINE=InnoDB;

INSERT IGNORE INTO wdt_territory VALUES
    ('river-crossing', 'River Crossing', 'WDTRIVER.MPR', 'GDI', 1),
    ('central-basin', 'Central Basin', 'WDTBASIN.MPR', 'Neutral', 2),
    ('ridge-line', 'Ridge Line', 'WDTRIDGE.MPR', 'Nod', 3),
    ('northern-pass', 'Northern Pass', 'WDTRIDGE.MPR', 'Neutral', 4),
    ('southern-flats', 'Southern Flats', 'WDTBASIN.MPR', 'Neutral', 5);

CREATE TABLE IF NOT EXISTS wdt_operator_audit (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    battle_id CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    operator_name VARCHAR(80) NOT NULL,
    action ENUM('approve','reject','cancel') NOT NULL,
    reason VARCHAR(200) NOT NULL,
    acted_at BIGINT UNSIGNED NOT NULL,
    FOREIGN KEY (battle_id) REFERENCES wdt_battle(id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS wdt_lobby_seat (
    battle_id CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    faction ENUM('GDI','Nod') NOT NULL,
    player_name VARCHAR(80) NOT NULL,
    invite_hash BINARY(32) NOT NULL UNIQUE,
    connection_json TEXT NOT NULL,
    checked_in_at BIGINT UNSIGNED NULL,
    joined_at BIGINT UNSIGNED NULL,
    PRIMARY KEY (battle_id, faction),
    FOREIGN KEY (battle_id) REFERENCES wdt_battle(id)
) ENGINE=InnoDB;
