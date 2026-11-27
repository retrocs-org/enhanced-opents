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
