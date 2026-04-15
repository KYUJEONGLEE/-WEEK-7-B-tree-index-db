INSERT INTO players (nickname, game_win_count, game_loss_count) VALUES ('player_000001', 10, 2);
INSERT INTO players (nickname, game_win_count, game_loss_count) VALUES ('player_000002', 20, 4);
INSERT INTO players (nickname, game_win_count, game_loss_count) VALUES ('player_000003', 10, 3);

SELECT nickname FROM players WHERE id = 2;
SELECT nickname FROM players WHERE game_win_count = 10;
SELECT nickname FROM players WHERE nickname = 'player_000002';
